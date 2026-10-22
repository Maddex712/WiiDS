"""Generate Galaxy's private lower-screen presentation during the integrated import.

Input is the player's extracted disc plus the cooker's output. Only code and
package dependencies are shipped; no pre-baked game models/fonts are bundled.
"""
from pathlib import Path
import importlib.util
import hashlib
import json
import os
import sys


def build(extracted_files, cooked_files, output, scripts_root=None):
    os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
    here=Path(__file__).resolve().parent
    scripts_root=Path(scripts_root) if scripts_root else here.parents[1]
    extracted_files, cooked_files, output=map(Path,(extracted_files,cooked_files,output))
    sys.path.insert(0,str(here))
    sys.path.insert(0,str(scripts_root/'reference/galaxy-android/tools'))
    try:
        import android_bridge
        from lowermap import smgarc
        smgarc.yaz0=android_bridge.yaz0_decompress
    except ImportError:
        pass

    def load(name):
        spec=importlib.util.spec_from_file_location('wiids_'+name.replace('-','_'),here/(name+'.py'))
        module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def invoke(module,*args):
        old=sys.argv
        try:
            sys.argv=[str(module.__file__),*map(str,args)]
            result=module.main()
            if result not in (None,0): raise RuntimeError('Lower-screen generation failed')
        finally:
            sys.argv=old

    print('Building the lower-screen artwork and fonts from your disc...')
    invoke(load('Extract-LowerArt'),'--data',cooked_files/'LayoutData','--out',output)
    invoke(load('Extract-LowerFonts'),cooked_files/'LayoutData/Font.arc',output/'fonts')
    print('Building the Gardens map...')
    invoke(load('Bake-LowerMaps'),'--files',extracted_files,'--out',output/'maps')
    print('Building the textured planets (this can take several minutes)...')
    planets=load('Bake-LowerPlanets')
    table=here/'factory.json'
    if table.is_file():
        factory=json.loads(table.read_text())
        planets.factory=lambda:factory
    invoke(planets,'--files',extracted_files,'--out',output/'planets')
    required=['planets/HeavensDoorGalaxy.planets','fonts/numberfont.json',
              'playerleft-mymarios.png','maps/PeachCastleGardenGalaxy.json']
    for name in required:
        if not (output/name).is_file():raise RuntimeError('Lower-screen output missing: '+name)
    files={p.relative_to(output).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
           for p in sorted(output.rglob('*')) if p.is_file() and not p.name.endswith('_atlas.png')}
    (output/'SETUP-MANIFEST.json').write_text(json.dumps(files,indent=2)+'\n')
    print('Lower-screen assets ready:',len(files),'files.')


if __name__=='__main__':
    if len(sys.argv)!=4:raise SystemExit('Usage: LowerAssets.py EXTRACTED_FILES COOKED_FILES OUTPUT')
    build(*sys.argv[1:])
