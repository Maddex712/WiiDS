"""Build a private host fixture from copied owner files; never edits the base."""
from pathlib import Path
import sys,shutil,importlib.util,json,hashlib
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/galaxy-kaizo'
def main():
    sys.path.insert(0,str(ROOT/'reference/galaxy-android/tools/cook'))
    import kaizo_patch,cook
    raw=OUT/'extracted';cooked=OUT/'GalaxyData.pending'
    if raw.exists() or cooked.exists():raise RuntimeError('Fixture exists; inspect it before choosing a new output')
    original=ROOT/'build/galaxy-thor/extracted'
    shutil.copytree(original,raw,ignore=shutil.ignore_patterns('MovieData'))
    print('Private raw disc copy ready',flush=True)
    kaizo_patch.apply(raw,ROOT/'build/galaxy-mod-research/KaizoMarioGalaxyRebalanced-V2.1.zip')
    assert cook.convert(str(raw),str(cooked),jobs=6,no_audio=False,with_movies=False)==0
    kaizo_patch.finish(raw,cooked)
    spec=importlib.util.spec_from_file_location('kaizo_lower',ROOT/'experiments/galaxy-thor/LowerAssets.py')
    lower=importlib.util.module_from_spec(spec);spec.loader.exec_module(lower)
    lower.build(raw/'files',cooked/'files',cooked/'ThorHud',ROOT)
    manifest={p.relative_to(cooked).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in cooked.rglob('*') if p.is_file()}
    (OUT/'private-data-sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('PRIVATE MOD DATA READY',len(manifest),flush=True)
if __name__=='__main__':main()
