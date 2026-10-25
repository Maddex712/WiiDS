"""Export the accepted dev7-based 1.1 source profile without changing the workbench."""
from pathlib import Path
import argparse, hashlib, json, sys
import export_release_source as export

ROOT = Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir',type=Path,default=ROOT/'build/release-1.1-candidate')
args=parser.parse_args()
OUT=args.output_dir.resolve()
assert OUT.is_relative_to((ROOT/'build').resolve())
PREVIEW = ROOT / 'build/feature-1.1/preview-dev9/source'
delta = {e['path']: e for e in json.loads((ROOT/'build/ra-casual/source-delta.json').read_text())['files']}
origins = {}
def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
for name,e in delta.items():
    assert sha(ROOT/name) == e['after_sha256'], name

def profile(path):
    name=path.relative_to(ROOT).as_posix()
    if name in delta:
        e=delta[name]
        if e['before_sha256'] is None:
            origins[name]={'action':'omit paused experiment addition'}
            return None
        before=ROOT/'build/ra-casual/before/source'/name
        assert sha(before) == e['before_sha256'], name
        origins[name]={'action':'restore pre-experiment source','sha256':sha(before)}
        return before
    if name.startswith('reference/driftdroid/android/app/src/main/') and (path.suffix in ('.java','.kt') or path.name=='AndroidManifest.xml'):
        staged=PREVIEW/name
        assert staged.is_file(), name
        # Source must match the private, achievement-preserving preview build.
        assert path.read_bytes()==staged.read_bytes(), name
        origins[name]={'action':'exact current private Android preview source','sha256':sha(staged)}
        return staged
    return path

candidate_files=export.candidates
coverage_files=export.expected_source_files
def mapped(generator):
    def walk(directory):
        for relative,path in generator(directory):
            selected=profile(path)
            if selected is not None: yield relative,selected
    return walk

export.candidates=mapped(candidate_files)
export.expected_source_files=mapped(coverage_files)
export.EXTRA_SOURCE += ('docs/FEATURE_PLAN_1_1.md','docs/TESTING_1_1_PREVIEW.md',
                        'docs/SAVE_MANAGER.md','docs/FRONTEND_LAUNCHING.md','docs/ACHIEVEMENTS.md')
OUT.mkdir(parents=True,exist_ok=True)
sys.argv=[__file__,'--output',str(OUT/'WiiDS-1.1-source-candidate.zip')]
export.main()
receipt={'status':'PASS','profile':'dev28 / dev7 achievement library; Dolphin imports, relative Galaxy trackpad, native camera restrictions, material animations and attack colors, corrected lighting, timed hint, single-tap menu, kart shortcut stack preservation and MSMBWii label; paused earning client omitted',
         'paused_files_verified_unchanged':len(delta),'source_choices':origins,
         'workbench_mutated':False,'published':False}
(OUT/'source-profile.json').write_text(json.dumps(receipt,indent=2)+'\n')
