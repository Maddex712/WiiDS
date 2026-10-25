"""Record completed pointer acceptance and local release candidates; no device or publication actions."""
from pathlib import Path
import hashlib, json
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/feature-1.1/galaxy-pointer-2026-10-25'
RELEASE=ROOT/'build/release-1.1-candidate'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
for e in json.loads((ROOT/'build/feature-1.1/thor-polish-2026-10-24/source-hashes.json').read_text())['files']:
    assert sha(ROOT/e['path'])==e['after_sha256'],e['path']
for e in json.loads((ROOT/'build/ra-casual/source-delta.json').read_text())['files']:
    assert sha(ROOT/e['path'])==e['after_sha256'],e['path']
after=json.loads((OUT/'after.json').read_text());assert after['checked']==48 and not after['changed']
candidate=json.loads((RELEASE/'candidate.json').read_text())
source=json.loads((RELEASE/'source-verification.json').read_text())
audit=json.loads((RELEASE/'APK-AUDIT.json').read_text())
assert candidate['status']=='PASS' and source['status']=='PASS' and audit['status']=='pass'
assert sha(Path(candidate['apk']))==candidate['sha256']==audit['apk_sha256']
assert sha(RELEASE/'WiiDS-1.1-source-candidate.zip')==source['archive_sha256']
result={
 'status':'POINTER_ACCEPTANCE_PASSED_LOCAL_RELEASE_CANDIDATES_READY',
 'tested_installed_version':'1.1-dev.15','installed_version_code':906,
 'original_native_game_pid':22826,'kaizo_native_game_pid':26586,
 'original_checks':['file-slot hover without automatic click','explicit A confirmation loads existing Luigi save',
     'normal movement and spin','Planet rotation without moving upper pointer','touch-to-right-stick handoff',
     'four sleep/wake cycles with pointer working afterward','Home and supported shortcut return','normal exit'],
 'kaizo_checks':['existing separate Yoshi-icon save loaded','three star bits collected by lower touch alone',
     'stationary Mario; no A/B during collection','pointer works after sleep/wake','normal exit'],
 'collection_scope':'Kaizo Castle Gardens uses the shared Galaxy pointer implementation. Original Gateway starting zone has no placed star bits; initial silver targets were butterflies.',
 'owner_acceptance':'Owner explicitly confirmed physical controls, NSMB tilt/shake and longer play work well on dev15 on 2026-10-25.',
 'all48_readable_external_files_unchanged':True,'paused_experiment_files_unchanged':91,
 'production_code_changes_this_pass':False,'candidate':candidate,'source':source,
 'published':False,'committed':False,'pushed':False,
 'device_handoff':'Both native games exited; final library capture retained. Owner subsequently opened Cocoon.',
 'open_user_question':'Owner asks why whole lower screen looks different. Current read-only capture shows Cocoon home grid. Asked what differs from the prior layout; no Cocoon theme/grid/scaling settings changed by this pass.',
 'limits':['No exhaustive all-stage/FPS/audio/haptic claim','Code1100 candidate not installed; tested native/game payload identical to dev15','No clean-machine source rebuild'],
}
(OUT/'status.json').write_text(json.dumps(result,indent=2)+'\n')
(RELEASE/'acceptance.json').write_text(json.dumps(result,indent=2)+'\n')
(RELEASE/'SHA256SUMS.txt').write_text(candidate['sha256']+'  '+Path(candidate['apk']).name+'\n'+source['archive_sha256']+'  WiiDS-1.1-source-candidate.zip\n')
entry='''2026-10-25 GALAXY POINTER ACCEPTANCE PASSED; LOCAL 1.1 CANDIDATES READY (not installed/published): Owner requested Galaxy pointer testing and final polish. Dev15 original Galaxy native PID22826 passed file-slot hover/no auto click, lower-touch+explicit A existing Luigi-slot load, normal movement/spin, Planet rotation, touch/right-stick handoff, four sleep/wake cycles, Home/shortcut return and normal exit. Original Gateway starting zone has no placed StarPiece objects; initially targeted silver shapes were butterflies, not a pointer defect. Kaizo separate Yoshi-icon save loaded in native PID26586; Castle Gardens lower-touch sweep collected3starbits with stationary Mario and no A/B. Pointer worked after Kaizo sleep/wake; normal exit. All48freshly-backed-up readable external save/settings files unchanged. Owner explicitly confirmed physical controls, NSMB tilt/shake and longer play work well. No production source changes needed this pass; all91paused experiment files unchanged. Local owner-signed/nondebuggable version1.1/code1100 APK build/release-1.1-candidate/WiiDS-1.1-arm64-candidate.apk SHA843b48c349743fcccea403b8444ec5288ea5a00633c63bb263e7b6f90ae85736/1847073667bytes; all25native+1474game/menu assets identical dev15, only manifest/classes.dex/baseline.prof changed. APK audit25libs/39source samples0findings PASS. Matching source candidate WiiDS-1.1-source-candidate.zip SHA77001858d6f14562dd3289d7b638cd7ae64648136be806e15daea2b3be0a713e/576018609bytes;14849entries/14847manifest hashes/11913source coverage0omissions. Export profile restores13pre-experiment source files, omits78paused additions; workbench unchanged. No clean-machine rebuild claim. Source archive and code1100 APK remain LOCAL, not installed; Thor stilldev15/code906. Both test games exited; final library screenshot retained. Owner subsequently opened Cocoon and asks why whole bottom screen looks different: current screenshot shows Cocoon home grid; asked what differs from previous layout, no Cocoon theme/grid/scaling mutations or further device input. Resolve this question before publication. Receipts/scoped logs/screens at build/feature-1.1/galaxy-pointer-2026-10-25, release audits/hashes at build/release-1.1-candidate; notes docs/release/1.1-{RELEASE-NOTES,SOURCE-PROFILE,THOR-POLISH}.md. Deleted only hash-verified superseded task dev10 APK for space; dev7rollback/dev15 retained. No commit/push/upload/publication.\n\n'''
agents=ROOT/'AGENTS.md';old=agents.read_text(encoding='utf-8')
if not old.startswith('2026-10-25 GALAXY POINTER ACCEPTANCE PASSED;'):
    agents.write_text(entry+old,encoding='utf-8')
print(json.dumps({'status':result['status'],'checks':'PASS','publication':False,'open_question':'Cocoon lower-screen appearance'}))
