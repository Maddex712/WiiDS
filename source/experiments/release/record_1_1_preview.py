"""Record the bounded local preview and source-only task delta; never access a device."""
from pathlib import Path
from datetime import datetime, timezone
import json
import hashlib
import difflib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'build/feature-1.1/save-manager'
PREVIEW = ROOT/'build/feature-1.1/preview-dev9'

def sha(file):
    h=hashlib.sha256()
    with file.open('rb') as stream:
        for block in iter(lambda:stream.read(1<<20),b''):h.update(block)
    return h.hexdigest()

audit=json.loads((PREVIEW/'apk-audit-dev10.json').read_text())
assert audit['status']=='PASS' and audit['paused_casual_client_absent'] and audit['native_resume_guard_included']
old=Path(audit['apk']);final=ROOT/'build/feature-1.1/WiiDS-1.1-dev.10.apk'
if old.exists():
    assert sha(old)==audit['sha256'] and not final.exists()
    old.replace(final)
assert sha(final)==audit['sha256']
audit['apk']=str(final)
(PREVIEW/'apk-audit-dev10.json').write_text(json.dumps(audit,indent=2)+'\n')

app='reference/driftdroid/android/app/src/main/'
files={
app+'java/com/driftdroid/android/saves/SaveArchive.java':None,
app+'kotlin/com/driftdroid/android/SaveManagerActivity.kt':None,
app+'kotlin/com/driftdroid/android/SaveManagerProfiles.kt':None,
app+'kotlin/com/driftdroid/android/ModePickerActivity.kt':OUT/'before/ModePickerActivity.kt',
app+'kotlin/com/driftdroid/android/ThorLauncherView.kt':OUT/'before/ThorLauncherView.kt',
app+'AndroidManifest.xml':OUT/'before/AndroidManifest.xml',
'reference/nsmbw-android-e1/runtime/src/hle/gx/gx_copy.cpp':ROOT/'build/feature-1.1/resume/before/gx_copy_e1.cpp',
'reference/nsmbw-android-msmbwii/runtime/src/hle/gx/gx_copy.cpp':ROOT/'build/feature-1.1/resume/before/gx_copy_msmbwii.cpp',
}
for name,fragment in {
    'FrontendLaunchActivity.kt':'        SaveManagerProfiles.blocker(this)?.let { fail(it); return }\n',
    'ThorGameLaunch.kt':'''        SaveManagerProfiles.blocker(activity)?.let {
            android.widget.Toast.makeText(activity, it, android.widget.Toast.LENGTH_LONG).show()
            return Result.UNAVAILABLE
        }
''',
}.items():
    relative=app+'kotlin/com/driftdroid/android/'+name
    current=(ROOT/relative).read_text();assert current.count(fragment)==1
    before=OUT/'before'/name
    before.write_text(current.replace(fragment,''),encoding='utf-8')
    files[relative]=before
for name in ['test_save_archive.py','test_save_profiles.py','test_nsmb_resume_copy.py',
             'package_1_1_preview.py','build_resume_preview.py','record_1_1_preview.py','NSMB_RESUME_GUARD.md']:
    files['experiments/release/'+name]=None
for name in ['SAVE_MANAGER.md','TESTING_1_1_PREVIEW.md']:
    files['docs/'+name]=None
patch=[]
for name,before in files.items():
    text=(ROOT/name).read_text(encoding='utf-8');oldtext=before.read_text(encoding='utf-8') if before else ''
    patch.extend(difflib.unified_diff(oldtext.splitlines(True),text.splitlines(True),
        fromfile='a/'+name if before else '/dev/null',tofile='b/'+name))
(OUT/'task-changes.patch').write_text(''.join(patch),encoding='utf-8')
# All paused source files still match the earlier experiment's receipt.
delta=json.loads((ROOT/'build/ra-casual/source-delta.json').read_text())
for entry in delta['files']:
    assert sha(ROOT/entry['path'])==entry['after_sha256'],entry['path']
sourcefiles=[name for name in files if name.startswith('reference/driftdroid/android/')]
for name in sourcefiles:
    staged=PREVIEW/'source'/name
    assert sha(staged)==sha(ROOT/name),f'Packaged UI differs from tested source: {name}'

status={
 'updated_utc':datetime.now(timezone.utc).isoformat(),'status':'Local dev10 preview built, owner-signed and audited; Thor testing pending',
 'apk':audit,
 'verification':{'save_archive_transaction_checks':60,'profile_and_launch_gate_checks':62,
    'frontend_resolver_checks':48,'launcher_touch_checks':47,'shortcut_checks':68,
    'resume_checks_per_native_variant':24,'native_variants':2,'total_checks':333,
    'actual_arm64_native_builds':'PASS','android_assemble_release':'PASS',
    'compiled_guard_disassembly':'PASS','original_resume_crash_fixed':'NOT ESTABLISHED'},
 'scope':['Shared native-save backups/export/restore for eight default profiles','Per-game Android shortcuts from previous pass',
          'NSMB/MSMBWii inactive-frame copy guard','Seven-row Options layout'],
 'preserved':{'paused_source_files':len(delta['files']),'dev7_achievement_behavior':True,'motion_sources_unchanged':True,
              'device_access':False,'owner_saves_changed':False,'commit_push_upload_publication':False},
 'pending':['Physical Thor dialogs and file-picker operations','Cold game reload after restore','Cocoon pin/launch',
            'NSMB/MSMBWii Home/screen-off/resume','Existing Galaxy pointer gameplay acceptance',
            'Dolphin adapters, favorites/recent history, wider controller consistency and measured presets'],
 'source_sha256':{name:sha(ROOT/name) for name in list(files)+['docs/FEATURE_PLAN_1_1.md']},
}
(OUT/'status.json').write_text(json.dumps(status,indent=2)+'\n',encoding='utf-8')
(final.with_suffix('.apk.sha256')).write_text(audit['sha256']+'  '+final.name+'\n',encoding='ascii')

entry=(
'2026-10-24 ANDROID 1.1 DEV10 LOCAL PREVIEW READY (no device/publication): Owner requested continuing quality-of-life fixes before Thor testing. '
'Shared Save manager (Options/fallback) now supports dated SHA-verified WiiDS backups/export/restore for all8default original/mod profiles; verified before-restore archive, bounded strict profile-bound ZIPs, sibling staging, atomic renames, journal rollback/committed-cleanup recovery, launch and active-game/importer/update gates. '
'Uses actual MKW PAL NAND, RR RetroWFC+RetroWFC2/Pulsar, NSMB/MSMB distinct title data, SMS/Eclipse card-a, Galaxy/Kaizo separate internal NAND+slot roots and external mirrors. '
'Accounts/achievements/motion/Mii/reserve/game assets excluded; custom NAND/extra NSMB profiles refused; Dolphin adapters not implemented. '
'Native source investigation found EnsureAuroraFrameActive retry timeout still entered GX copy; actual old wrapper regression FAIL, new display+texture-entry guards24checks each E1/MSMB PASS, both actual ARM64 compile/link and branch disassembly PASS. '
'This does NOT prove the recorded Thor GXCopyDisp crash is fixed; full device logs not retrieved. '
'Owner-signed/nondebuggable code906 1.1-dev.10 build/feature-1.1/WiiDS-1.1-dev.10.apk '+audit['sha256']+'/'+str(audit['bytes'])+'bytes; vs installed dev7 only NSMB/MSMB native + dex/manifest/profiles differ,23other native+1474game/presentation assets exact. '
'Private frontend build copy restores exact recorded dev7 achievement sources; native E1 rebuild uses dev7 VI and omits paused Casual source/archive; no Casual Java classes/native exports, all paused workbench source hashes unchanged. '
'60actual filesystem/archive/recovery+62profile/gates+48frontend+47touch+68shortcut+48resume=333checks PASS, Android assemble PASS. '
'No adb/device access/install/saves changes, no commit/push/upload/publication. Last installed remains dev7; dev8 experiment untouched. '
'Receipts/backups/taskpatch build/feature-1.1/{save-manager,resume,preview-dev9}; final APK is dev10, dev9 temporary predecessor may be removed. '
'Thor acceptance checklist docs/TESTING_1_1_PREVIEW.md; docs/SAVE_MANAGER.md; experiments/release/NSMB_RESUME_GUARD.md. '
'Future remaining: Dolphin adapters, favorites/recent, controller consistency, measured presets; Galaxy pointer acceptance pending. Earlier iOS/private-data checkpoint preserved.\n\n')
agents=ROOT/'AGENTS.md';text=agents.read_text(encoding='utf-8')
if not text.startswith('2026-10-24 ANDROID 1.1 DEV10 LOCAL PREVIEW READY'): agents.write_text(entry+text,encoding='utf-8')

# Remove only this task's verified duplicate APKs; retain installed dev7 and dev8 experiment.
for duplicate,expected in [
    (PREVIEW/'WiiDS-1.1-dev.9-save-shortcuts.apk','36be36b40a4498ff3afeb256e78a584f24d21a87175ab32eb0914d3c21b34483'),
    (PREVIEW/'source/reference/driftdroid/android/app/build/outputs/apk/release/app-release.apk',audit['sha256']),
]:
    if duplicate.exists():
        assert sha(duplicate)==expected
        duplicate.unlink()
print(json.dumps({'apk':str(final),'sha256':audit['sha256'],'checks':333,'installed':False,'published':False},indent=2))
