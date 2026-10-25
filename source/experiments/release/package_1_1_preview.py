"""Package frontend-only 1.1 improvements using the verified installed dev7 baseline.

The active workbench also retains a paused Casual-client experiment. Build in a
private source copy, restore only its recorded dev7 baseline files in that copy,
and reuse every native library and game asset byte from dev7. Never install here.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess
import zipfile
import argparse

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/feature-1.1/preview-dev9'
SOURCE = OUT / 'source'
ANDROID = SOURCE / 'reference/driftdroid/android'
BASE = ROOT / 'build/ra-casual/before/WiiDS-dev7.apk'
BASE_SHA = '736c6e788f3b40d42df95e8e4cafa597d0e26fa7e1be711066e76dddaa6b2c41'

def sha(file):
    h = hashlib.sha256()
    with file.open('rb') as stream:
        for part in iter(lambda: stream.read(1 << 20), b''): h.update(part)
    return h.hexdigest()

def prepare():
    assert sha(BASE) == BASE_SHA, 'dev7 baseline must match installed checkpoint'
    assert not SOURCE.exists(), 'Refuse to overwrite an earlier preview source copy'
    ANDROID.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / 'reference/driftdroid/android', ANDROID,
        ignore=shutil.ignore_patterns('build', '.gradle', '.cxx', 'jniLibs', 'assets', '.git'))
    delta = json.loads((ROOT / 'build/ra-casual/source-delta.json').read_text(encoding='utf-8'))
    restored = []
    for entry in delta['files']:
        name = entry['path']
        if not name.startswith('reference/driftdroid/android/'): continue
        current = ROOT / name
        assert sha(current) == entry['after_sha256'], f'Paused experiment changed independently: {name}'
        target = SOURCE / name
        if entry['before_sha256'] is None:
            if target.exists(): target.unlink()
        else:
            before = ROOT / 'build/ra-casual/before/source' / name
            assert sha(before) == entry['before_sha256'], f'Bad dev7 source backup: {name}'
            target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(before, target)
        restored.append(name)
    # Package exactly the existing runtime libraries and assets. No CMake placeholder
    # or uninstalled experiment may supply a native library to this frontend build.
    with zipfile.ZipFile(BASE) as z:
        for info in z.infolist():
            if info.is_dir(): continue
            if info.filename.startswith('lib/arm64-v8a/'):
                target = ANDROID / 'app/src/main/jniLibs/arm64-v8a' / Path(info.filename).name
            elif info.filename.startswith('assets/') and not info.filename.startswith('assets/dexopt/'):
                target = ANDROID / 'app/src/main/assets' / info.filename[len('assets/'):]
            else: continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as source, target.open('wb') as destination: shutil.copyfileobj(source, destination, 1 << 20)
    build = ANDROID / 'app/build.gradle.kts'
    text = build.read_text(encoding='utf-8')
    native = '''    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.28.3"
        }
    }'''
    assert text.count(native) == 1
    text = text.replace(native, '    // Private frontend preview: all 25 native libraries are verified copies of dev7.')
    assert text.count('assets.srcDir(') == 3
    text = '\n'.join(line for line in text.splitlines() if 'assets.srcDir(' not in line) + '\n'
    build.write_text(text, encoding='utf-8')
    (OUT / 'source-baseline.json').write_text(json.dumps({
        'baseline_sha256': BASE_SHA, 'dev7_source_files_restored_in_private_copy': restored,
        'all_workbench_files_preserved': True, 'native_source_build': False,
        'native_and_game_assets': 'All copied unchanged from the dev7 APK',
        'resume_native_guard_in_apk': False,
    }, indent=2) + '\n', encoding='utf-8')

def build(resume=False, polish=False, revision=False):
    version = f'1.1-dev.{revision}' if revision else ('1.1-dev.11' if polish else ('1.1-dev.10' if resume else '1.1-dev.9'))
    with (OUT / ('build-polish.log' if polish else ('build-resume.log' if resume else 'build.log'))).open('w', encoding='utf-8') as log:
        run = subprocess.run([str(ANDROID / 'gradlew.bat'), ':app:assembleRelease',
            '-PthorBuild=true', '-PwiidsApp=true', '-PthorRetroRewind=true',
            '-PwiidsVersionName='+version, '-PwiidsVersionCode=906', '--no-daemon', '--max-workers=4',
            '--console=plain'], cwd=ANDROID, stdout=log, stderr=subprocess.STDOUT)
    assert run.returncode == 0, 'Preview build failed; inspect build.log'
    apk = OUT / (f'WiiDS-{version}.apk' if polish else ('WiiDS-1.1-dev.10.apk' if resume else 'WiiDS-1.1-dev.9-save-shortcuts.apk'))
    shutil.move(ANDROID / 'app/build/outputs/apk/release/app-release.apk', apk)
    return apk

def audit(apk, resume=False, polish=False, revision=False):
    unchanged_libs = []; unchanged_assets = []; changed = []; added = []; removed = []
    with zipfile.ZipFile(BASE) as before, zipfile.ZipFile(apk) as after:
        old = set(before.namelist()); new = set(after.namelist())
        for name in old & new:
            if name.endswith('/'): continue
            same = hashlib.sha256(before.read(name)).digest() == hashlib.sha256(after.read(name)).digest()
            if name.startswith('lib/'):
                if resume and name in ('lib/arm64-v8a/libnsmbw.so','lib/arm64-v8a/libmsmbwii.so'):
                    staged = ANDROID/'app/src/main/jniLibs/arm64-v8a'/Path(name).name
                    assert hashlib.sha256(after.read(name)).hexdigest() == sha(staged)
                    assert not same, f'Guarded runtime did not change: {name}'
                else:
                    assert same, f'Native runtime changed: {name}'
                    unchanged_libs.append(name)
            elif name.startswith('assets/') and not name.startswith('assets/dexopt/'):
                assert same, f'Game/presentation asset changed: {name}'
                unchanged_assets.append(name)
            if not same: changed.append(name)
        for name in old - new:
            assert not name.startswith(('lib/', 'assets/')), f'Removed baseline content: {name}'
            removed.append(name)
        for name in new - old:
            assert not name.startswith(('lib/', 'assets/')), f'Added native/asset content: {name}'
            added.append(name)
        assert len(unchanged_libs) == (23 if resume else 25), 'Unexpected native library delta'
        dex = b''.join(after.read(name) for name in new if name.endswith('.dex'))
        for absent in [b'RaEarningSetup', b'RaEarningConnection', b'RetroAchievementsSession', b'RaClientHttp', b'RaDiscProof']:
            assert absent not in dex, f'Paused client class present: {absent!r}'
        assert b'GameShortcuts' in dex and b'SaveManagerActivity' in dex and b'SaveArchive' in dex
    tools = ROOT / '.tools/android-sdk/build-tools/34.0.0'
    signature = subprocess.run([str(tools / 'apksigner.bat'), 'verify', '--verbose', '--print-certs', str(apk)], capture_output=True, text=True, check=True)
    assert '72eef06b956bc1c6848a09bf4956b158095736a6e81f43131cf93cb30788345f' in signature.stdout
    (OUT/'signature.txt').write_text(signature.stdout,encoding='utf-8')
    badging = subprocess.run([str(tools/'aapt.exe'),'dump','badging',str(apk)],capture_output=True,text=True,check=True)
    version = f'1.1-dev.{revision}' if revision else ('1.1-dev.11' if polish else ('1.1-dev.10' if resume else '1.1-dev.9'))
    assert f"versionName='{version}'" in badging.stdout and "versionCode='906'" in badging.stdout
    assert 'application-debuggable' not in badging.stdout
    (OUT/'badging.txt').write_text(badging.stdout,encoding='utf-8')
    report = {
        'updated_utc': datetime.now(timezone.utc).isoformat(), 'status': 'PASS', 'apk': str(apk),
        'sha256': sha(apk), 'bytes': apk.stat().st_size, 'baseline_sha256': BASE_SHA,
        'unchanged_native_libraries': len(unchanged_libs), 'unchanged_game_and_presentation_assets': len(unchanged_assets),
        'changed_entries': sorted(changed), 'added_entries': sorted(added), 'removed_entries': sorted(removed),
        'owner_signed': True, 'nondebuggable': True, 'paused_casual_client_absent': True,
        'native_resume_guard_included': resume, 'device_access': False, 'installed': False, 'published': False,
    }
    (OUT/(f'apk-audit-dev{revision}.json' if revision else ('apk-audit-dev11.json' if polish else ('apk-audit-dev10.json' if resume else 'apk-audit.json')))).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--finish-resume',action='store_true')
    parser.add_argument('--polish',action='store_true',help='Build the Thor-verified storage fix and save screen using the private dev7 source profile')
    parser.add_argument('--single-tap',action='store_true',help='Apply the final save-button touch correction over the recorded dev11 source')
    parser.add_argument('--resume-shortcut',action='store_true',help='Build task-only shortcut resume over the recorded dev12 source')
    parser.add_argument('--native-probe',action='store_true',help='Package Android host-PC crash diagnostics over the existing dev13 source')
    parser.add_argument('--overlay-guard',action='store_true',help='Package the real ImGui frame-lifetime fix in both NSMB runtimes')
    args = parser.parse_args()
    if args.overlay_guard:
        native = ROOT/'build/feature-1.1/thor-polish-2026-10-24/native-fixed'
        nsmb = json.loads((native/'verification.json').read_text())
        msmb = json.loads((native/'msmbwii/verification.json').read_text())
        assert nsmb['status']=='PASS' and nsmb['overlay_frame_guard'] and nsmb['paused_client_code_omitted']
        assert msmb['status']=='PASS'
        assert sha(native/'libnsmbw.so')==nsmb['libnsmbw_sha256']
        assert sha(native/'msmbwii/libmsmbwii.so')==msmb['libmsmbwii_sha256']
        with zipfile.ZipFile(OUT/'WiiDS-1.1-dev.14.apk') as previous:
            for name,source in [('libnsmbw.so',native/'libnsmbw.so'),('libmsmbwii.so',native/'msmbwii/libmsmbwii.so')]:
                target = ANDROID/'app/src/main/jniLibs/arm64-v8a'/name
                assert sha(target)==hashlib.sha256(previous.read('lib/arm64-v8a/'+name)).hexdigest()
                shutil.copy2(source,target)
        shutil.copy2(OUT/'build-polish.log',OUT/'build-dev14.log')
        audit(build(True,True,15),True,True,15)
    elif args.native_probe:
        native = ROOT/'build/feature-1.1/thor-polish-2026-10-24/native-probe'
        receipt = json.loads((native/'verification.json').read_text())
        assert receipt['status']=='PASS' and receipt['paused_client_code_omitted']
        assert sha(native/'libnsmbw.so')==receipt['libnsmbw_sha256']
        target = ANDROID/'app/src/main/jniLibs/arm64-v8a/libnsmbw.so'
        previous = json.loads((ROOT/'build/feature-1.1/resume/dev7-native/verification.json').read_text())
        assert sha(target)==previous['libnsmbw_sha256']
        shutil.copy2(native/'libnsmbw.so',target)
        shutil.copy2(OUT/'build-polish.log',OUT/'build-dev13.log')
        audit(build(True,True,14),True,True,14)
    elif args.resume_shortcut:
        relative = 'reference/driftdroid/android/app/src/main/'
        for name in ['AndroidManifest.xml','kotlin/com/driftdroid/android/FrontendLaunchActivity.kt']:
            target = SOURCE/relative/name
            before = ROOT/'build/feature-1.1/thor-polish-2026-10-24/dev12-source'/name
            assert sha(target)==sha(before), f'Unexpected staged frontend change: {name}'
            shutil.copy2(ROOT/relative/name,target)
        name = 'java/com/driftdroid/android/FrontendResume.java'
        assert not (SOURCE/relative/name).exists()
        shutil.copy2(ROOT/relative/name,SOURCE/relative/name)
        shutil.copy2(OUT/'build-polish.log',OUT/'build-dev12.log')
        audit(build(True,True,13),True,True,13)
    elif args.polish or args.single_tap:
        relative = 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/'
        for name in ['SaveManagerProfiles.kt','SaveManagerActivity.kt','ThorLowerCompanion.kt']:
            target = SOURCE/relative/name
            before = ROOT/'build/feature-1.1/thor-polish-2026-10-24'/('dev11-source' if args.single_tap else 'before')/name
            assert sha(target)==sha(before), f'Unexpected change in staged preview file: {name}'
            shutil.copy2(ROOT/relative/name,target)
        revision = 12 if args.single_tap else 0
        audit(build(True,True,revision),True,True,revision)
    elif args.finish_resume:
        native = ROOT/'build/feature-1.1/resume/dev7-native'
        receipt = json.loads((native/'verification.json').read_text())
        assert receipt['status'] == 'PASS' and receipt['paused_client_code_omitted']
        assert sha(native/'libnsmbw.so') == receipt['libnsmbw_sha256']
        binaries = {
            'libnsmbw.so':native/'libnsmbw.so',
            'libmsmbwii.so':ROOT/'build/nsmbw-thor/msmbwii/android-native/libmsmbwii.so',
        }
        for name,source in binaries.items():
            target = ANDROID/'app/src/main/jniLibs/arm64-v8a'/name
            shutil.copy2(source,target)
            subprocess.run([str(ROOT/'.tools/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'),'--strip-unneeded',str(target)],check=True)
        audit(build(True), True)
    else:
        prepare()
        audit(build())
