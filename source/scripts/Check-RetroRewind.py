"""Read-only prerequisites for the Thor's optional standalone Retro Rewind build.

Does not fetch packs, generate translations, change the phone, or prove gameplay.
Exit 2 means required local inputs are missing/incompatible; exit 0 means they
are ready for translation, not that the mod has been built or tested.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
RUNTIME = ROOT / 'reference/driftdroid'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--pack', type=Path, help='Extracted RetroRewind6 folder')
parser.add_argument('--output', type=Path, help='Optional local JSON receipt')
args = parser.parse_args()
pack = (args.pack or RUNTIME / 'PulsarPacks/completed/RetroRewind/RetroRewind6').resolve()


def sha256(path):
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


release = RUNTIME / 'android/app/src/main/kotlin/com/driftdroid/android/RetroRewindRelease.kt'
match = re.search(r'const val CODE_PUL_SHA256\s*=\s*"([0-9a-f]{64})"', release.read_text(encoding='utf-8'))
if not match:
    raise SystemExit('Cannot read the pinned frontend Code.pul hash')
expected = match[1]
code = pack / 'Binaries/Code.pul'
actual = sha256(code)
checks = {
    'code_pul': {'path': str(code), 'sha256': actual, 'expected_sha256': expected,
                 'valid': actual == expected},
    'riivolution_xml': {'path': str(pack / 'xml/RetroRewind6.xml'),
                        'valid': (pack / 'xml/RetroRewind6.xml').is_file()},
    'tracks': {'path': str(pack / 'Tracks'), 'valid': (pack / 'Tracks').is_dir()},
}
for name in ('ConfigRT.pul', 'ConfigCT.pul', 'ConfigBT.pul'):
    path = pack / 'Binaries' / name
    checks[name] = {'path': str(path), 'valid': path.is_file()}
for name, checksum in {
    'main.dol': '80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05',
    'StaticR.rel': '16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d',
}.items():
    path = RUNTIME / 'Assets' / name
    digest = sha256(path)
    checks[name] = {'path': str(path), 'sha256': digest, 'expected_sha256': checksum,
                    'valid': digest == checksum}

metadata = RUNTIME / 'generated/base_translation_output.json'
awareness = json.loads(metadata.read_text(encoding='utf-8')).get('modPatchAwareness', []) if metadata.is_file() else []
graph = RUNTIME / 'generated/build_shards/shards.cmake'
text = graph.read_text(encoding='utf-8') if graph.is_file() else ''
count = re.search(r'set\(MKW_RETRO_REWIND_FUNCTION_COUNT\s+(\d+)\)', text)
report = {
    'pack_root': str(pack),
    'checks': checks,
    'ready_to_translate': all(check['valid'] for check in checks.values()),
    'generated_state': {
        'base_mod_awareness': awareness,
        'rr_functions': int(count[1]) if count else 0,
        'rr_enabled': bool(re.search(r'set\(MKW_HAVE_RETRO_REWIND_SHARDS\s+ON\)', text)),
        'resolved_profile_exists': (RUNTIME / 'build/mods/retro_rewind_full_cpp/resolved_dispatch_profile.json').is_file(),
        'native_library_exists': (ROOT / 'build/android-native/libRetroRewind.so').is_file(),
    },
    'gameplay_verified': False,
    'lower_custom_map_verified': False,
}
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({'ready_to_translate': report['ready_to_translate'],
                  'missing_or_incompatible': [key for key, value in checks.items() if not value['valid']],
                  'generated_state': report['generated_state']}, indent=2))
raise SystemExit(0 if report['ready_to_translate'] else 2)
