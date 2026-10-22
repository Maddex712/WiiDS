"""Record exact private adaptations without committing the shared workbench."""
from pathlib import Path
import difflib
import hashlib
import json
import subprocess
import os
import tempfile

root = Path(__file__).resolve().parents[2]
experiment = Path(__file__).resolve().parent
patches = experiment / 'patches'
patches.mkdir(exist_ok=True)
native = root / 'reference/galaxy-android'
pin = '1abcb975927ecb4189949bd51f81e73a04d97566'
assert subprocess.check_output(['git', '-C', str(native), 'rev-parse', 'HEAD'], text=True).strip() == pin
patch = subprocess.check_output(['git', '-C', str(native), 'diff', '--binary', pin, '--'])
# New files (the lower screen's sources) are untracked: add them as creations without
# touching the workbench index (git diff --no-index exits 1 when the files differ).
for relative in subprocess.check_output(['git', '-C', str(native), 'ls-files', '--others', '--exclude-standard'],
                                        text=True).splitlines():
    patch += subprocess.run(['git', '-C', str(native), 'diff', '--no-index', '--binary', '--', '/dev/null', relative],
                            stdout=subprocess.PIPE).stdout
(patches / 'galaxy-android.patch').write_bytes(patch)
subprocess.run(['git', '-C', str(native), 'apply', '--reverse', '--check',
                str(patches / 'galaxy-android.patch')], check=True)
subprocess.run(['git', '-C', str(native), 'diff', '--cached', '--quiet', pin], check=True)
subprocess.run(['git', '-C', str(native), 'apply', '--cached', '--check',
                str(patches / 'galaxy-android.patch')], check=True)

baseline = root / 'build/github-publication/WiiDS'
public_pin = '60feb5cabe0b29878eda94394118fa9d48112f12'
paths = ['android/app/src/main/AndroidManifest.xml']
paths += ['android/app/src/main/kotlin/com/driftdroid/android/' + name + '.kt' for name in
          ('ThorGameLaunch', 'ModePickerActivity', 'ThorLauncherArtwork', 'ThorLibraryCompanionView')]
combined = []
records = []
for relative in paths:
    prior = subprocess.check_output(['git', '-C', str(baseline), 'show',
        public_pin + ':source/reference/driftdroid/' + relative]).decode('utf-8').replace('\r\n', '\n')
    current = (root / 'reference/driftdroid' / relative).read_text(encoding='utf-8')
    combined.extend(difflib.unified_diff(prior.splitlines(True), current.splitlines(True),
        fromfile='a/' + relative, tofile='b/' + relative))
    records.append({'path': relative, 'sha256': hashlib.sha256(current.encode()).hexdigest()})
(patches / 'wiids-galaxy.patch').write_text(''.join(combined), encoding='utf-8', newline='\n')
# The public archive preserves CRLF in two source blobs; ignore context-only
# line-ending whitespace. A disposable index keeps release staging untouched.
with tempfile.TemporaryDirectory(prefix='wiids-patch-check-') as temporary:
    env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / 'index'))
    subprocess.run(['git', '-C', str(baseline), 'read-tree', public_pin], env=env, check=True)
    subprocess.run(['git', '-C', str(baseline), 'apply', '--cached', '--check', '--ignore-space-change',
                   '--directory=source/reference/driftdroid', str(patches / 'wiids-galaxy.patch')], env=env, check=True)
receipt = {'nativeBase': pin, 'wiidsBase': public_pin, 'wiidsFiles': records,
           'patches': [{'file': p.name, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                       for p in sorted(patches.glob('*.patch'))], 'result': 'PASS'}
(root / 'build/galaxy-thor/source-verification.json').write_text(json.dumps(receipt, indent=2))
print('PASS: native forward/reverse and five WiiDS patch application checks')
