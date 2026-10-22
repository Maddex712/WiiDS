"""Fetch pinned Android Python wheels used to generate the player's lower screen."""
from pathlib import Path
import hashlib
import json
import shutil
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).parent
OUT=ROOT/'build/galaxy-thor/python-packages'


def main():
    manifest=json.loads((HERE/'python-packages.json').read_text())
    OUT.mkdir(parents=True,exist_ok=True)
    libs=ROOT/'build/galaxy-thor/importer/libs'
    libs.mkdir(parents=True,exist_ok=True)
    site=OUT/'site-packages'
    site.mkdir(exist_ok=True)
    for item in manifest['wheels']:
        p=OUT/item['name']
        if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=item['sha256']:
            with urllib.request.urlopen(item['url'],timeout=120) as response:
                data=response.read()
            if hashlib.sha256(data).hexdigest()!=item['sha256']:raise RuntimeError('Wheel hash mismatch: '+p.name)
            p.write_bytes(data)
        with zipfile.ZipFile(p) as z:
            for name in z.namelist():
                if '..' in Path(name).parts or Path(name).is_absolute():raise RuntimeError('Unsafe wheel path')
            z.extractall(site)
    # Reuse the host's newer libc++; never replace it with Chaquopy's older copy.
    for name in ['libgfortran.so.3','libopenblas.so','libjpeg_chaquopy.so','libfreetype.so']:
        shutil.copy2(site/'chaquopy/lib'/name,libs/('libgfortran.so' if name=='libgfortran.so.3' else name))
    print('Android NumPy/Pillow packages verified:',len(manifest['wheels']))


if __name__=='__main__':main()
