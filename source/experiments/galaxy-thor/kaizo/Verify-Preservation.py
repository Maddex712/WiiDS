from pathlib import Path
import zipfile,hashlib,json
R=Path(__file__).resolve().parents[3];O=R/'build/galaxy-kaizo'
before=R/'build/public-release-1.0/WiiDS-1.0-arm64.apk';after=O/'WiiDS-kaizo-dev-arm64.apk'
def hashes(path):
 with zipfile.ZipFile(path) as z:return {n:hashlib.sha256(z.read(n)).hexdigest() for n in z.namelist()}
a,b=hashes(before),hashes(after)
other_native=[n for n in a if n.startswith('lib/') and Path(n).name not in ['libgame.so','libinstaller.so']]
changed_native=[n for n in other_native if a[n]!=b.get(n)]
installed_sms=O/'thor-before/libsms.so'
preserved_sms='lib/arm64-v8a/libsms.so' in changed_native and installed_sms.is_file() and hashlib.sha256(installed_sms.read_bytes()).hexdigest()==b['lib/arm64-v8a/libsms.so']
assert not [n for n in changed_native if not (preserved_sms and n=='lib/arm64-v8a/libsms.so')],changed_native
other_assets=[n for n in a if n.startswith('assets/') and not n.startswith(('assets/galaxy/','assets/dexopt/'))]
changed_assets=[n for n in other_assets if a[n]!=b.get(n)]
assert not changed_assets,changed_assets
report={'status':'PASS','unchanged_other_native_libraries':len(other_native)-len(changed_native),'unchanged_other_assets':len(other_assets),
 'installed_Thor_Sunshine_variant_preserved':preserved_sms,
 'changed_entries':[n for n in a if a[n]!=b.get(n)],'added_entries':sorted(set(b)-set(a)),
 'published_apk_sha256':hashlib.sha256(before.read_bytes()).hexdigest(),
 'candidate_apk_sha256':hashlib.sha256(after.read_bytes()).hexdigest(),
 'mod_patch_bundled':any('KaizoMarioGalaxy' in n or n.endswith('.wiids-kaizo.zip') for n in b)}
assert not report['mod_patch_bundled']
(O/'apk-preservation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: native preservation (including installed Sunshine variant if needed);',len(other_assets),'other assets byte-identical')
