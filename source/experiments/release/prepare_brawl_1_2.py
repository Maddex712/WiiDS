"""Regenerate Brawl's excluded maps/layout privately from public symbols and the owner's disc.

Only --regenerate writes derived game inputs. It uses a new private output directory and
never changes the retained engine, disc or public metadata. Generated game bytes are NOT exported.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root', type=Path, default=Path('M:/WiiDS-brawl/src'))
    p.add_argument('--modules', type=Path, default=Path('M:/WiiDS-brawl/extract/files/module'))
    p.add_argument('--config', type=Path)
    p.add_argument('--main-dol', type=Path)
    p.add_argument('--handler-inputs', type=Path)
    p.add_argument('--private-root', type=Path, default=Path('M:/WiiDS-release-wrap-2026-11-08'))
    p.add_argument('--output', type=Path)
    p.add_argument('--regenerate', action='store_true')
    a = p.parse_args()
    data = json.loads((HERE / 'brawl_1_2_regeneration.json').read_text())
    source = a.source_root.resolve()
    private = a.private_root.resolve()
    config = a.config or source / 'extras/brawl-decomp/config/RSBE01_02'
    dol = a.main_dol or source / 'game/main.dol'
    output = (a.output or private / 'package-review/brawl-regeneration').resolve()
    permitted = private / 'package-review'
    if permitted not in output.parents or output == permitted:
        raise ValueError('Derived game output must remain below the private package-review directory')
    if sha(config / 'symbols.txt') != data['public_symbols_sha256'] or sha(dol) != data['dol_sha256']:
        raise ValueError('Exact public symbol metadata and owner RSBE01 revision2 main.dol are required')
    modules = sorted(a.modules.glob('*.rel'))
    if len(modules) != 126:
        raise ValueError('Expected all126 owner-supplied REL modules')
    # Capture input identities without copying or publishing the modules.
    inputs = {'main_dol_sha256': sha(dol), 'modules': {x.name: sha(x) for x in modules},
              'public_symbols_sha256': data['public_symbols_sha256'], 'public_config_pin': data['public_config_pin']}
    if not a.regenerate:
        print(json.dumps({'status': 'INPUTS_VERIFIED', 'module_count': len(modules), 'output': str(output), 'generated': False}))
        return
    if output.exists():
        raise ValueError('Refusing to overwrite a regeneration directory; select a new private directory')
    wc = output / 'wiicompiled'
    project = wc / 'projects/brawl'
    (project / 'tools').mkdir(parents=True)
    (project / 'gecko').mkdir()
    (wc / 'runtime/src/hle').mkdir(parents=True)
    (output / 'game').mkdir()
    rows = [(int(m[3], 16), m[1]) for line in (config / 'symbols.txt').read_text().splitlines()
            if (m := re.match(r'(\S+) = (\.\w+):0x([0-9A-Fa-f]+); // type:function', line))]
    for edit in data['symbol_name_overrides']:
        index = edit['index']
        if rows[index] != (int(edit['address'], 16), edit['before']):
            raise ValueError('Symbol override input mismatch')
        rows[index] = (rows[index][0], edit['after'])
    (project / 'MAP.txt').write_text(''.join(f'{address:08X} {name}\n' for address, name in rows))
    handler_source = a.handler_inputs or private / 'package-review/upstream-codehandler'
    h = data['handler']
    if sha(handler_source / 'codehandleronly.h') != h['header_sha256']:
        raise ValueError('Pinned upstream USBLoaderGX handler source header is required')
    if sha(handler_source / 'codehandleronly.s') != h['assembly_sha256']:
        raise ValueError('Pinned GeckoOS assembly source is required alongside the upstream representation')
    body = (handler_source / 'codehandleronly.h').read_text().split('{', 1)[1].split('}', 1)[0]
    handler = bytearray(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))
    for edit in h['word_patches']:
        at = edit['offset']
        if handler[at:at + 4].hex() != edit['before']:
            raise ValueError('Handler patch input mismatch')
        handler[at:at + 4] = bytes.fromhex(edit['after'])
    if hashlib.sha256(handler).hexdigest() != h['result_sha256']:
        raise ValueError('Regenerated non-Nintendo Gecko handler differs from the accepted input')
    (project / 'gecko/codehandler.bin').write_bytes(handler)
    for name in ('build_modules.py', 'prelink_rels.py'):
        shutil.copyfile(source / 'wiicompiled/projects/brawl/tools' / name, project / 'tools' / name)
    subprocess.run([sys.executable, '-B', str(project / 'tools/build_modules.py'), str(a.modules),
                    str(config), str(output / 'game/brawl_modules.rel'), str(wc), str(dol)], check=True)
    checks = {}
    for name, expected in data['expected_outputs'].items():
        path = wc / name
        normalized = hashlib.sha256(path.read_text().replace('\r\n', '\n').encode()).hexdigest()
        if normalized != expected['text_sha256']:
            raise ValueError('Regenerated metadata differs from accepted source: ' + name)
        checks[name] = {'sha256': sha(path), 'normalized_text_exact': True}
    for name, expected in data['expected_binary_outputs'].items():
        if sha(output / 'game' / name) != expected:
            raise ValueError('Private translation input differs from accepted input: ' + name)
        checks['game/' + name] = {'sha256': expected, 'byte_exact': True}
    result = {'status': 'PASS', 'inputs': inputs, 'outputs': checks, 'originals_changed': False,
              'generated_outputs_are_private': True, 'translated_game_functions_generated': False, 'published': False}
    (output / 'verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': 'PASS', 'module_count': len(modules), 'verified_outputs': len(checks), 'output': str(output)}))


if __name__ == '__main__':
    main()
