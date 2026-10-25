"""Add scoped native semantics for all Rebalanced 2.1 NTSC code sites.

Each substitution asserts the source it adapts; existing unrelated edits remain.
Private pre-edit bytes are saved once. No source index or public clone is touched.
"""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[3]
SRC=ROOT/'reference/galaxy-android'
BACKUP=ROOT/'build/galaxy-kaizo/before'
records=[]
def edit(path, substitutions):
    file=SRC/path;raw=file.read_bytes();text=raw.decode('utf-8')
    if '#include "port/kaizo.h"' in text:
        print('Already adapted:',path);return
    backup=BACKUP/path;backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists():backup.write_bytes(raw)
    newline='\r\n' if '\r\n' in text else '\n'
    text=text.replace('\r\n','\n')
    for old,new in substitutions:
        assert text.count(old)==1,(path,old,text.count(old))
        text=text.replace(old,new)
    text='#include "port/kaizo.h"\n'+text
    file.write_bytes(text.replace('\n',newline).encode('utf-8'));records.append(path)
base='decomp/src/Game/'
path=base+'MapObj/FlipPanel.cpp'
# There are precisely two equivalent capacity sites in this compilation unit.
f=SRC/path;raw=f.read_bytes()
if '#include "port/kaizo.h"' not in raw.decode('utf-8'):
 b=BACKUP/path;b.parent.mkdir(parents=True,exist_ok=True);b.write_bytes(raw)
 assert raw.count(b'MR::joinToGroupArray(this, rIter, 0, 0x20);')==2
 f.write_bytes(b'#include "port/kaizo.h"\r\n'+raw.replace(b'MR::joinToGroupArray(this, rIter, 0, 0x20);',b'MR::joinToGroupArray(this, rIter, 0, port_kaizo_enabled() ? 0x80 : 0x20);'))
 records.append(path)
edit(base+'Enemy/WaterBazooka.cpp',[(
 'shotWaterBullet(this, posMtx, 40.0f, true, true, false, &mCameraInfo);',
 'shotWaterBullet(this, posMtx, 40.0f, true, true, port_kaizo_enabled(), &mCameraInfo);')])
edit(base+'Boss/BossKameckBarrier.cpp',[(
 'MR::initDefaultPos(this, rIter);',
 'MR::initDefaultPos(this, rIter);\n    if (port_kaizo_enabled()) mPosition.y += -1500.0f; // XML 0x80001800 contains -1500.0f, called at 0x800382B8.')])
edit(base+'Boss/SkeletalFishBaby.cpp',[(
 'MR::appearAirBubble(rAirBubblePos, -1);',
 'if (!port_kaizo_enabled()) MR::appearAirBubble(rAirBubblePos, -1);')])
for level in ['1','2']:
 edit(base+'Boss/DodoryuStateLv'+level+'.cpp',[(
 'if (MR::isMsgPlayerHitAll(msg)) {',
 'if (port_kaizo_enabled() ? MR::isMsgJetTurtleAttack(msg) : MR::isMsgPlayerHitAll(msg)) {')])
for actor in ['BegomanBaby','BegomanSpike','BegomanSpring']:
 edit(base+'Enemy/'+actor+'.cpp',[(
 'if (MR::isBindedGroundDamageFire(this)) {',
 'if (!port_kaizo_enabled() && MR::isBindedGroundDamageFire(this)) {')])
edit(base+'NPC/KinopioAstro.cpp',[
 ('if (MR::isLuigiLeftSupply()) {','if (port_kaizo_enabled() || MR::isLuigiLeftSupply()) {'),
 ('i < ::sMessangerOneUpExNum;','i < (port_kaizo_enabled() ? 99 : ::sMessangerOneUpExNum);'),
 ('requestOneUp(::sMessangerOneUpExNum);','requestOneUp(port_kaizo_enabled() ? 99 : ::sMessangerOneUpExNum);'),
 ('MR::setMessageArg(mMsgCtrl, ::sMessangerOneUpExNum);','MR::setMessageArg(mMsgCtrl, port_kaizo_enabled() ? 99 : ::sMessangerOneUpExNum);')])
edit(base+'Util/SceneUtil.cpp',[
 ('return isEqualStageName("CosmosGardenGalaxy") && getCurrentScenarioNo() == 4;',
  'return !port_kaizo_enabled() && isEqualStageName("CosmosGardenGalaxy") && getCurrentScenarioNo() == 4;'),
 ('s32 scenarioNo = getPlacedHiddenStarScenarioNoFromTable(pStageName, starId);',
  's32 scenarioNo = port_kaizo_enabled() ? 0 : getPlacedHiddenStarScenarioNoFromTable(pStageName, starId);')])
edit(base+'Util/SequenceUtil.cpp',[(
 'GameSequenceFunction::requestGalaxyMove(argument);',
 'GameSequenceFunction::requestGalaxyMove(argument);\n        if (port_kaizo_enabled()) GameDataFunction::addStarPiece(-999);'),
 ('#include "Game/System/GameSequenceFunction.hpp"','#include "Game/System/GameSequenceFunction.hpp"\n#include "Game/System/GameDataFunction.hpp"')])
edit(base+'System/GalaxyCometScheduler.cpp',[(
 'new GalaxyCometTimeTable(sGalaxyCometTimerPaper[i]);',
 'new GalaxyCometTimeTable(port_kaizo_comet_table(i, sGalaxyCometTimerPaper[i]));')])
edit(base+'System/GameEventFlagTable.cpp',[
 ('static const GameEventFlag sGameEventFlagStatic[] = {','static GameEventFlag sGameEventFlagStatic[] = {'),
 ('    initSortTable();','    port_kaizo_apply_flags(::sGameEventFlagStatic, ARRAY_SIZE(::sGameEventFlagStatic));\n    initSortTable();')])
edit('platform/mobile/mobile_app.cpp',[(
 '    mobile_core::MobileApp::get().run(app, windowBase, windowSize);',
 '    port_kaizo_configure(app->activity->internalDataPath, app->activity->externalDataPath);\n    mobile_core::MobileApp::get().run(app, windowBase, windowSize);')])
# A previous hardcoded log sink bypassed the scoped Activity path. Logs must also
# stay in the selected profile; fallback semantics otherwise remain identical.
edit('platform/mobile/core/MobileApp.cpp',[(
 'std::string primaryLog = "/sdcard/Android/data/io.github.maddex712.wiids/files/galaxy/GalaxyData/galaxy_log.txt";',
 'std::string primaryLog = ext + "/GalaxyData/galaxy_log.txt";')])
edit('platform/src/dvd/dvd_disc.cpp',[(
 '    for (const PortDisc& disc : kDiscs) {\n        if (hasTexts(root, fst, disc.languages[0])) {',
 '    for (const PortDisc& disc : kDiscs) {\n        // The mod overlays PAL text folders too; they do not change its USA base disc.\n        if (port_kaizo_enabled() && strcmp(disc.id, "RMGE01")) continue;\n        if (hasTexts(root, fst, disc.languages[0])) {')])
records=sorted(p.relative_to(BACKUP).as_posix() for p in BACKUP.rglob('*') if p.is_file())
(ROOT/'build/galaxy-kaizo/native-adapted-files.json').write_text(json.dumps(records,indent=2)+'\n')
print('Scoped native semantics adapted:',len(records),'files')
