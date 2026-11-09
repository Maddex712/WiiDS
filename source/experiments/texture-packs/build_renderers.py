"""Build reviewed texture-cache adapters using retained pinned native compile/link commands.

Outputs are private. The stripped baseline must match the verified APK before any build.
No APK installation, source-tree mutation or publication.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import zipfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def build_id_span(data):
    if data[:5] != b'\x7fELF\x02' or data[5] != 1:
        raise ValueError('Expected little-endian ELF64')
    offset = struct.unpack_from('<Q', data, 40)[0]
    size, count, names = struct.unpack_from('<HHH', data, 58)
    entries = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * size) for i in range(count)]
    table = data[entries[names][4]:entries[names][4] + entries[names][5]]
    for entry in entries:
        if table[entry[0]:].split(b'\0', 1)[0] == b'.note.gnu.build-id':
            return entry[4], entry[5]
    raise ValueError('Missing ELF build ID')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target', choices=('mkdd', 'plus', 'galaxy'), required=True)
    p.add_argument('--commands', type=Path, required=True)
    p.add_argument('--baseline-apk', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--link-root', type=Path, help='Rebuilt object tree; all stripped bytes except GNU build ID must match the APK')
    p.add_argument('--extra-source', type=Path, action='append', default=[], help='Additional isolated native source replacement')
    a = p.parse_args()
    private = a.output.resolve()
    repo = Path(__file__).resolve().parents[2]
    if private == repo or repo in private.parents:
        p.error('Native outputs must stay outside the source tree')
    target = a.target
    config = {
        'galaxy': ('galaxy-integration', 'gx_recorder_tex.cpp', 'libgame.so', 'libgame.so', '--strip-unneeded'),
        'mkdd': ('WiiDS-mkdd-2026-11-01', 'texture.cpp', 'libwiids_mkdd_game.so', 'libwiids_mkdd_game.so', '--strip-debug'),
        'plus': ('plus-2026-11-03', 'texture.cpp', 'libwiids_mkdd_game.so', 'libwiids_mkdd_plus_game.so', '--strip-debug'),
    }
    name, source_name, build_name, package_name, strip_mode = config[target]
    commands = json.loads((a.commands / f'{name}-commands.json').read_text(encoding='utf-8-sig'))
    compile = next(c for c in commands if c.get('output', '').endswith('/' + source_name + '.o'))
    link = next(c for c in commands if c.get('output') == build_name)
    cwd = a.link_root.resolve() if a.link_root else Path(link['directory'])
    out = private / target
    out.mkdir(parents=True, exist_ok=True)
    compiler = compile['command'].split()[0]
    strip = Path(compiler).with_name('llvm-strip.exe')
    baseline = out / 'baseline.so'
    # Gradle strips the private staging library again when packaging the APK.
    subprocess.run([strip, '--strip-unneeded', '-o', baseline, cwd / build_name], check=True)
    with zipfile.ZipFile(a.baseline_apk) as apk:
        packaged = apk.read('lib/arm64-v8a/' + package_name)
        expected = hashlib.sha256(packaged).hexdigest()
    normalized = baseline.read_bytes()
    if a.link_root:
        start, length = build_id_span(normalized)
        old_start, old_length = build_id_span(packaged)
        if (start, length) != (old_start, old_length):
            raise ValueError('Rebuilt baseline ELF layout changed')
        normalized = normalized[:start] + packaged[start:start + length] + normalized[start + length:]
    if hashlib.sha256(normalized).hexdigest() != expected:
        raise ValueError('Baseline native objects differ from the verified APK; refusing to link')
    object_path = (out / (source_name + '.o')).as_posix()
    source_path = (private / source_name).as_posix()
    cmd = compile['command'].replace(compile['output'], object_path).replace(compile['file'], source_path)
    cmd += ' -I"' + Path(compile['file']).parent.as_posix() + '"'
    subprocess.run(cmd, cwd=cwd, check=True)
    cmd = link['command'].split('cd . && ', 1)[1].rsplit(' && cd .', 1)[0]
    cmd = cmd.replace(compile['output'], object_path).replace('-o ' + build_name + ' ', '-o ' + (out / build_name).as_posix() + ' ', 1)
    for extra in a.extra_source:
        record = next(c for c in commands if c.get('output', '').endswith('/' + extra.name + '.o'))
        obj = (out / (extra.name + '.o')).as_posix()
        build = record['command'].replace(record['output'], obj).replace(record['file'], extra.resolve().as_posix())
        build += ' -I"' + Path(record['file']).parent.as_posix() + '"'
        subprocess.run(build, cwd=cwd, check=True)
        cmd = cmd.replace(record['output'], obj)
    linker, args = cmd.split(' ', 1)
    rsp = out / 'link.rsp'
    rsp.write_text(args, encoding='utf-8')
    subprocess.run([linker, '@' + str(rsp)], cwd=cwd, check=True)
    package = out / package_name
    # Galaxy's build name is its package name: strip via a separate destination.
    package = out / ('package-' + package_name)
    subprocess.run([strip, strip_mode, '-o', package, out / build_name], check=True)
    receipt = {'target': target, 'baseline_sha256': expected, 'new_sha256': digest(package),
               'rebuilt_baseline_sha256': digest(baseline), 'baseline_matches_except_build_id': bool(a.link_root),
               'package': str(package), 'source_sha256': {n: digest(private / n) for n in
                (source_name, 'texture_pack.h', 'stb_image.h', 'bcdec.h')}}
    receipt['extra_sources'] = {str(source): digest(source) for source in a.extra_source}
    (out / 'verification.json').write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
