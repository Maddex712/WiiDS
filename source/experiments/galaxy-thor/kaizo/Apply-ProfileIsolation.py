"""Route inherited native profile I/O through the scoped Activity paths."""
from pathlib import Path
import re,json
R=Path(__file__).resolve().parents[3]; S=R/'reference/galaxy-android'
B=R/'build/galaxy-kaizo/isolation-before'; B.mkdir(exist_ok=True)
prefix='/sdcard/Android/data/io.github.maddex712.wiids/files/galaxy/GalaxyData'
paths=['platform/src/nand/nand.cpp','platform/src/port/boot.cpp',
 'platform/mobile/ui/SaveManager.cpp','platform/mobile/ui/SaveSyncManager.cpp',
 'platform/mobile/render/VulkanLifecycle.cpp','platform/mobile/system/SystemManager.cpp',
 'platform/src/gx/gx_recorder_tex.cpp','platform/src/gx/vk_shader_manifest.cpp',
 'platform/src/gx/vk/vk_pipeline_cache_manager.cpp','platform/mobile/core/MobileApp.cpp',
 'platform/mobile/core/GameBooter.cpp','platform/mobile/mobile_app.cpp',
 'platform/mobile/ui/SettingsMenuDefs.cpp','platform/mobile/ui/SettingsMenuModActions.cpp',
 'platform/mobile/ui/SettingsDrawerRender.cpp']
for name in paths:
    p=S/name; raw=p.read_bytes();t=raw.decode().replace('\r\n','\n')
    if '#include "port/profile.h"' in t:continue
    b=B/name;b.parent.mkdir(parents=True,exist_ok=True);b.write_bytes(raw)
    if name.endswith('nand.cpp'):
        t=t.replace('port_log("nand: atomically mirrored %s -> '+prefix+'/saves/GameData.bin", hp.c_str());',
          'port_log("nand: atomically mirrored %s -> %s", hp.c_str(), port_profile_data_path("/saves/GameData.bin").c_str());')
        t=t.replace('// Auto-mirror GameData.bin to persistent external storage ('+prefix+'/saves) atomically',
          '// Atomically mirror GameData.bin within this Activity profile only.')
    if name.endswith('gx_recorder_tex.cpp'):
        t=t.replace('port_log("textures: Custom texture overrides enabled from '+prefix+'/textures");',
          'port_log("textures: Custom texture overrides enabled from %s", port_profile_data_path("/textures").c_str());')
    if 'SettingsMenu' in name or name.endswith('SettingsDrawerRender.cpp'):
        t=t.replace(prefix+'/mods','the active Galaxy profile mods folder')
        t=t.replace(prefix+'/saves','the active Galaxy profile saves folder')
    t=re.sub(r'"'+re.escape(prefix)+r'([^"\n]*)"',lambda m:'port_profile_data_path("'+m[1]+'").c_str()',t)
    if name.endswith('vk_pipeline_cache_manager.cpp'):
        t=t.replace('snprintf(sPipelineCachePath, sizeof(sPipelineCachePath), "/data/data/io.github.maddex712.wiids/files/galaxy/vk_pipeline_cache.bin");',
          'snprintf(sPipelineCachePath, sizeof(sPipelineCachePath), "%s", port_profile_internal_path("/vk_pipeline_cache.bin").c_str());')
    if name.endswith('MobileApp.cpp'):
        t=t.replace('"/data/data/io.github.maddex712.wiids/files/galaxy"','port_profile_internal_root()')
        t=t.replace('"/sdcard/Android/data/io.github.maddex712.wiids/files/galaxy"','std::string(port_profile_data_root()).substr(0, std::string(port_profile_data_root()).size() - 11)')
    if name.endswith('GameBooter.cpp'):
        t=t.replace('Waiting for game files in '+prefix+' or %s/game (attempt %d)',
          'Waiting for game files in the active profile or %s/game (attempt %d)')
    if name.endswith('mobile_app.cpp'):
        t=t.replace('    port_kaizo_configure(', '    port_profile_configure(app->activity->internalDataPath, app->activity->externalDataPath);\n    port_kaizo_configure(')
    if name.endswith('SaveSyncManager.cpp'):
        t=re.sub(r'"/sdcard/Download([^"\n]*)"',lambda m:'port_profile_exchange_path("'+m[1]+'").c_str()',t)
        t=t.replace('    mkdir(port_profile_exchange_path("").c_str(), 0770);',
          '    mkdir("/sdcard/Download", 0770);\n    mkdir(port_profile_exchange_path("").c_str(), 0770);')
        # c_str() temporaries cannot outlive this statement in a candidates array.
        t=t.replace('const char* downloadCandidates[] = {','const std::string downloadCandidates[] = {')
        t=t.replace('for (const char* path : downloadCandidates) {',
          'for (const std::string& candidate : downloadCandidates) {\n        const char* path = candidate.c_str();\n        if (port_profile_exchange_path("").find("WiiDS-Kaizo") != std::string::npos && candidate.find("/sdcard/dolphin-emu/") == 0) continue;')
        t=t.replace('outToastMsg = "Exported to Download/ (includes Dolphin save)!";',
          'outToastMsg = "Exported to " + port_profile_exchange_path("") + " (includes Dolphin save)!";')
    t='#include "port/profile.h"\n'+t
    p.write_bytes(t.replace('\n','\r\n' if b'\r\n' in raw else '\n').encode())
report={'modified':paths,'obsolete_unbuilt_reserve_c':'platform/android/reserve.c is outside PLATFORM_SRCS and no libreserve.so is packaged; its obsolete wrap.sh driver hooks are not used.'}
(R/'build/galaxy-kaizo/profile-path-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('Profile paths adapted:',len(paths),'native files')
