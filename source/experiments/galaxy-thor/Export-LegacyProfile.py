"""Create a private, copy-only ZIP for WiiDS's Import Galaxy backup screen.

Supply copies of the old helper's external files/ directory and internal files/nand.
For the old debuggable helper these can be copied with adb pull and adb exec-out
run-as io.github.maddex712.wiids.galaxy tar, respectively. Never uninstall/clear it.
The resulting ZIP contains the player's game/save data: do not publish it.
"""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--external',type=Path,required=True)
    p.add_argument('--nand',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args()
    if args.out.exists():p.error('Choose a new output ZIP; existing backups are preserved')
    files={}
    for prefix,folder in [('external/GalaxyData',args.external/'GalaxyData'),
                          ('external/GalaxyData/ThorHud',args.external/'ThorHud'),
                          ('internal/nand',args.nand)]:
        if not folder.is_dir():p.error('Missing private input directory: '+str(folder))
        for path in folder.rglob('*'):
            if path.is_symlink():p.error('Backup input may not contain links')
            if path.is_file() and not path.name.endswith(('.log','.prev')) and path.name!='planet-preview':
                files[prefix+'/'+path.relative_to(folder).as_posix()]=path
    manifest={n:hashlib.sha256(path.read_bytes()).hexdigest() for n,path in files.items()}
    lower='external/GalaxyData/ThorHud/'
    hud={n.removeprefix(lower):sha for n,sha in manifest.items() if n.startswith(lower)}
    setup=(json.dumps(hud,indent=2)+'\n').encode()
    manifest[lower+'SETUP-MANIFEST.json']=hashlib.sha256(setup).hexdigest()
    args.out.parent.mkdir(parents=True,exist_ok=True)
    temporary=args.out.with_suffix('.zip.new')
    if temporary.exists():p.error('Temporary backup already exists')
    with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,compresslevel=1) as z:
        for name,path in sorted(files.items()):
            if name!=lower+'SETUP-MANIFEST.json':z.write(path,name)
        z.writestr(lower+'SETUP-MANIFEST.json',setup)
        z.writestr('BACKUP-MANIFEST.properties',''.join(n+'='+sha+'\n' for n,sha in sorted(manifest.items())))
    with zipfile.ZipFile(temporary) as z:
        if z.testzip():raise RuntimeError('Backup ZIP failed CRC verification')
    temporary.rename(args.out)
    print('Private backup verified:',args.out,'files:',len(manifest))

if __name__=='__main__':main()
