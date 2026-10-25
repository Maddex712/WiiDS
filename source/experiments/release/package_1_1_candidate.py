"""Build a local 1.1/code1100 candidate from the physically tested dev15 profile; never install/upload."""
from pathlib import Path
import argparse, hashlib, json, re, shutil, subprocess, zipfile
ROOT = Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir',type=Path,default=ROOT/'build/release-1.1-candidate')
parser.add_argument('--version-name',default='1.1')
parser.add_argument('--version-code',type=int,choices=[906,1100],default=1100)
parser.add_argument('--picture-receipt',type=Path,help='Verified local Galaxy picture-mode native receipt')
args=parser.parse_args()
OUT=args.output_dir.resolve()
assert OUT.is_relative_to((ROOT/'build').resolve())
ANDROID = ROOT / 'build/feature-1.1/preview-dev9/source/reference/driftdroid/android'
BASE = ROOT / 'build/feature-1.1/WiiDS-1.1-dev.15.apk'
SHA = 'e433c2d49d07e243836467409e17f6c2db78d0fc624571334ee655a6e6930584'
def sha(path):
    with path.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
def main():
    OUT.mkdir(parents=True, exist_ok=True)
    assert args.version_name=='1.1' or re.fullmatch(r'1\.1-dev\.\d{1,3}',args.version_name)
    target = OUT / ('WiiDS-1.1-arm64-candidate.apk' if args.version_name=='1.1' else 'WiiDS-'+args.version_name+'.apk')
    assert not target.exists(), 'Refuse to replace a candidate'
    assert sha(BASE) == SHA
    delta = json.loads((ROOT/'build/ra-casual/source-delta.json').read_text())['files']
    for e in delta:
        assert sha(ROOT/e['path']) == e['after_sha256'], e['path']
        if e['path'].startswith('reference/driftdroid/android/'):
            staged = ROOT/'build/feature-1.1/preview-dev9/source'/e['path']
            assert (sha(staged) if staged.exists() else None) == e['before_sha256'], e['path']
    picture=None
    if args.picture_receipt:
        receipt=args.picture_receipt.resolve();assert receipt.is_relative_to((ROOT/'build').resolve())
        picture=json.loads(receipt.read_text());assert picture['status']=='PASS'
        assert sha(receipt.parent/'libgame.so')==picture['libgame_sha256']
        for name,e in picture['source_delta'].items():
            assert sha(ROOT/'reference/galaxy-android'/name)==e['after']
            assert sha(ROOT/'build/galaxy-integration/source'/name)==e['after']
    with zipfile.ZipFile(BASE) as z:
        for name in z.namelist():
            if name.startswith('lib/arm64-v8a/') and not name.endswith('/'):
                staged = ANDROID/'app/src/main/jniLibs/arm64-v8a'/Path(name).name
                expected=hashlib.sha256(z.read(name)).hexdigest()
                if picture and name=='lib/arm64-v8a/libgame.so':
                    assert expected==picture['baseline_sha256'];expected=picture['libgame_sha256']
                assert sha(staged) == expected, name
    command = [str(ANDROID/'gradlew.bat'), ':app:assembleRelease', '-PthorBuild=true',
               '-PwiidsApp=true', '-PthorRetroRewind=true', '-PwiidsVersionName='+args.version_name,
               '-PwiidsVersionCode='+str(args.version_code), '--offline', '--no-daemon', '--max-workers=4', '--console=plain']
    with (OUT/'build.log').open('w', encoding='utf-8') as log:
        run = subprocess.run(command, cwd=ANDROID, stdout=log, stderr=subprocess.STDOUT)
    assert run.returncode == 0, 'Build failed; inspect build.log'
    shutil.move(ANDROID/'app/build/outputs/apk/release/app-release.apk', target)
    changed=[]; same_native=[]; same_assets=[]
    with zipfile.ZipFile(BASE) as before, zipfile.ZipFile(target) as after:
        assert set(before.namelist()) == set(after.namelist())
        for name in before.namelist():
            if name.endswith('/'): continue
            same=hashlib.sha256(before.read(name)).digest() == hashlib.sha256(after.read(name)).digest()
            if not same: changed.append(name)
            if name.startswith('lib/'):
                if picture and name=='lib/arm64-v8a/libgame.so':
                    assert hashlib.sha256(after.read(name)).hexdigest()==picture['libgame_sha256']
                else:
                    assert same, name
                    same_native.append(name)
            if name.startswith('assets/') and not name.startswith('assets/dexopt/'):
                assert same, name
                same_assets.append(name)
    allowed=['AndroidManifest.xml','classes.dex','classes2.dex','assets/dexopt/baseline.prof','assets/dexopt/baseline.profm']
    if picture:allowed.append('lib/arm64-v8a/libgame.so')
    assert all(n in allowed for n in changed), changed
    tools=ROOT/'.tools/android-sdk/build-tools/34.0.0'
    certificate=subprocess.check_output([str(tools/'apksigner.bat'),'verify','--print-certs',str(target)],text=True)
    assert '72eef06b956bc1c6848a09bf4956b158095736a6e81f43131cf93cb30788345f' in certificate
    badging=subprocess.check_output([str(tools/'aapt.exe'),'dump','badging',str(target)],text=True)
    assert ("versionCode='"+str(args.version_code)+"'") in badging and ("versionName='"+args.version_name+"'") in badging
    assert 'application-debuggable' not in badging
    (OUT/'certificate.txt').write_text(certificate)
    (OUT/'badging.txt').write_text(badging)
    result={'status':'PASS','apk':str(target),'sha256':sha(target),'bytes':target.stat().st_size,
            'version_name':args.version_name,'version_code':args.version_code,'owner_signed':True,'nondebuggable':True,
            'tested_preview_sha256':SHA,'identical_native_libraries':len(same_native),
            'identical_game_and_menu_assets':len(same_assets),'changed_zip_entries':changed,
            'paused_workbench_files_preserved':len(delta),'installed':False,'published':False,
            'picture_native_receipt':str(args.picture_receipt) if picture else None,
            'device_scope':'New Dolphin adapters and Galaxy picture controls require their separate checks; unchanged native games retain the dev15 payload.'}
    (OUT/'candidate.json').write_text(json.dumps(result,indent=2)+'\n')
    target.with_suffix('.apk.sha256').write_text(result['sha256']+'  '+target.name+'\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__': main()
