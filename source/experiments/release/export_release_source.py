"""Export reviewed source roots, never the private workbench or game/generated data.

Creates an inspectable source candidate and SHA-256 inventory. This is not a
licence clearance or a claim that a clean-machine rebuild has passed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
# Each checkout is exported at its current working tree, with its upstream pin
# recorded separately. Local modifications and new authored files are included.
REPOS = (
    'driftdroid', 'nsmbw-android-e1', 'nsmbw-android-msmbwii',
    'sms-thor', 'sms-launcher-thor', 'sms-eclipse-thor',
    'eclipse-launcher-thor', 'galaxy-android',
)
DERIVED = {
    'nsmbw-android-e1': 'nsmbw-android',
    'nsmbw-android-msmbwii': 'nsmbw-android',
    'eclipse-launcher-thor': 'sms-launcher-thor',
}
DEPENDENCIES = {
    'third_party/eclipse': ('build/eclipse-thor/android-native/eclipse-src/eclipse',
        'https://github.com/JoshuaMKW/Super-Mario-Eclipse', '792e14fa1583910673ddae7e85048c6ddb4ba700'),
    'third_party/BetterSunshineEngine': ('build/eclipse-thor/android-native/eclipse-src/bse',
        'https://github.com/JoshuaMKW/BetterSunshineEngine', 'bc6840e373d59a2601e7b5f9011732390561f6dd'),
    'third_party/BetterSunshineMoveset': ('build/eclipse-thor/android-native/eclipse-src/moveset',
        'https://github.com/JoshuaMKW/BetterSunshineMoveset', '2eb6f136cce4c0c7816808ad9dc3d1d95ef52b83'),
    'third_party/SunshineHeaderInterface': ('build/eclipse-thor/android-native/eclipse-src/shi',
        'https://github.com/JoshuaMKW/SunshineHeaderInterface', 'a0d858951e7fb22dce5304aa5c50287ecb0d6862'),
    'third_party/SDL2-wiids': ('build/sunshine-thor/deps/SDL2-2.32.10-wiids',
        'https://github.com/libsdl-org/SDL', 'release-2.32.10'),
}
LOCAL_ROOTS = ('scripts', 'diagnostics', 'wiids', 'release', 'docs/release', 'experiments/release',
               'experiments/eclipse-thor', 'experiments/nsmbw-thor',
               'experiments/sunshine-thor', 'experiments/launcher-spines', 'experiments/galaxy-thor')
EXTRA_SOURCE = ('build/nsmbw-thor/rebuild-wiids-names.sh',
                'build/sunshine-thor/build-wiids-sdl2.sh')
EXTENSIONS = set('''.c .cc .cpp .cxx .h .hh .hpp .hxx .inl .inc .s .asm
 .cs .csproj .sln .props .targets .kt .kts .java .gradle .xml .cmake .py
 .ps1 .sh .bat .cmd .bzl .bazel .yml .yaml .toml .cfg .ini .txt .md
 .rst .json .jsonc .properties .pro .def .rc .patch .diff .m .mm .metal
 .glsl .vert .frag .wgsl .comp .ts .js .mjs .html .css .natvis'''.split())
NAMES = {'CMakeLists.txt', 'Makefile', 'Dockerfile', 'LICENSE', 'COPYING',
         'NOTICE', 'AUTHORS', 'THANKS', 'PATENTS', 'configure', 'gradlew',
         '.gitmodules', '.gitignore', '.gitattributes'}
# Infrastructure names can occur anywhere. Data names MUST NOT: Android's
# handwritten importer lives in a package literally called "rom", and game,
# saves, cache, etc. are perfectly valid engine source namespaces.
INFRASTRUCTURE_PARTS = {'.git', '.gradle', '.idea', '.vs', 'node_modules',
                        '__pycache__', '.cxx', 'jnilibs'}
PRIVATE_ROOTS = {'build', 'build-debug', 'build-release', 'generated', 'discdata',
    'extracted', 'rom', 'roms', 'game', 'games', 'nand', 'saves', 'save',
    'card-a', 'card-b', 'decomp', 'assets', 'cache', 'logs', 'before',
    'case-art', 'retail-art', 'source-art', 'supplied-metadata'}
PRIVATE_PREFIXES = (('android', 'build'), ('android', 'app', 'build'),
    ('android', 'native', 'build'), ('android', 'native', 'assets'),
    ('android', 'app', 'src', 'main', 'assets'),
    ('runtime', 'assets'))
SOURCE_SUFFIXES = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx',
                   '.kt', '.kts', '.java', '.cs', '.py', '.ps1', '.sh'}
PRESENTATION_ROOT = ROOT / 'build/wiids-merge/assets/presentation'
PRESENTATION_SUFFIXES = {'.png', '.jpg', '.jpeg', '.wav', '.json', '.rgba', '.tsv'}
PRESENTATION_TARGETS = {'launcher', 'launcher-audio', 'thor-map', 'nsmbw-hud',
                        'msmbwii-hud', 'sms-hud', 'eclipse-hud'}
DENIED_NAMES = {'local.properties', 'keystore.properties', 'settings.txt',
                'config.toml', 'verification.json', 'receipt.json', 'agents.md'}
SECRETS = re.compile(rb'(?:-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\r?\n[A-Za-z0-9+/]{32,}|'
    rb'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|'
    rb'AKIA[0-9A-Z]{16}|sk-(?:proj-)?[A-Za-z0-9_-]{40,})')


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(['git', '-C', str(repo), *args], capture_output=True)
    if result.returncode:
        raise RuntimeError(f'Git metadata unavailable: {repo.name}: {args[0]}')
    return result.stdout.decode('utf-8', errors='strict').strip()


def directory_allowed(relative: str) -> bool:
    parts = tuple(p.lower() for p in PurePosixPath(relative).parts if p != '.')
    if any(p in INFRASTRUCTURE_PARTS or p.startswith('cmake-build-') for p in parts):
        return False
    if parts and parts[0] in PRIVATE_ROOTS:
        return False
    return not any(parts[:len(prefix)] == prefix for prefix in PRIVATE_PREFIXES)


def eligible(relative: str) -> bool:
    path = PurePosixPath(relative)
    if not directory_allowed(path.parent.as_posix()):
        return False
    if path.name.lower() in DENIED_NAMES or path.name.startswith('.env'):
        return False
    if path.name.lower().startswith(('data_sections_init', 'data_sections_runtime', 'ppc_func_')):
        return False
    return (path.suffix.lower() in EXTENSIONS or path.name in NAMES
            or path.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE')))


def candidates(directory: Path):
    # Deliberately walk only named roots; prune disallowed trees before descent.
    import os
    for parent, dirs, files in os.walk(directory, followlinks=False):
        # Galaxy's decomp is handwritten CC0 source, unlike generated/private
        # decomp trees in other engines. Keep this exception scoped to that repo.
        def allowed(relative):
            if directory.name == 'galaxy-android' and relative.startswith('decomp/'):
                return directory_allowed(relative.removeprefix('decomp/'))
            return directory_allowed(relative)
        dirs[:] = sorted(d for d in dirs if (allowed(
                             (Path(parent) / d).relative_to(directory).as_posix())
                             or (directory.name == 'galaxy-android' and parent == str(directory) and d == 'decomp'))
                         and not (Path(parent) / d).is_symlink())
        for name in sorted(files):
            path = Path(parent) / name
            relative = path.relative_to(directory).as_posix()
            if directory == ROOT / 'experiments/galaxy-thor' and name in {
                    'CODEX_HANDOFF.md', 'CLAUDE_BOTTOM_SCREEN_HANDOFF.md'}:
                continue
            check = relative.removeprefix('decomp/') if directory.name == 'galaxy-android' else relative
            if eligible(check):
                yield relative, path


def expected_source_files(directory: Path):
    """Independent coverage inventory, deliberately not calling eligible().

    Enumerate code under each selected tree, skipping actual data/output roots
    and exact Android output/assets prefixes. Generic source namespaces are
    never removed. This gate would have caught the original rom-package bug.
    """
    import os
    for parent, dirs, files in os.walk(directory, followlinks=False):
        kept = []
        for name in dirs:
            path = Path(parent) / name
            relative = path.relative_to(directory)
            parts = tuple(p.lower() for p in relative.parts)
            if path.is_symlink() or name.lower() in INFRASTRUCTURE_PARTS:
                continue
            if name.lower().startswith('cmake-build-'):
                continue
            if len(parts) == 1 and parts[0] in PRIVATE_ROOTS and not (
                    directory.name == 'galaxy-android' and parts[0] == 'decomp'):
                continue
            if parts in PRIVATE_PREFIXES:
                continue
            kept.append(name)
        dirs[:] = sorted(kept)
        for name in sorted(files):
            path = Path(parent) / name
            if path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            if name.lower().startswith(('data_sections_init', 'data_sections_runtime', 'ppc_func_')):
                continue
            yield path.relative_to(directory).as_posix(), path


def add_presentation(entries):
    """Include only the explicitly curated manifest's presentation payloads.

    This exception cannot recursively scoop up other ignored build/assets files.
    Keep the staging path so the source snapshot reproduces Gradle's asset tree.
    """
    root = PRESENTATION_ROOT
    if not root.resolve().is_relative_to(ROOT.resolve()):
        raise RuntimeError('Presentation staging root escapes the workspace')
    manifest_path = root / 'manifest.json'
    if manifest_path.is_symlink() or not manifest_path.is_file():
        raise RuntimeError('The release presentation manifest has not been staged')
    raw_manifest = manifest_path.read_bytes()
    manifest = json.loads(raw_manifest)
    if manifest.get('schema') != 1 or manifest.get('kind') != 'wiids-bundled-presentation':
        raise RuntimeError('Unrecognised bundled presentation manifest')
    files = manifest.get('files')
    revision = manifest.get('revision')
    if not isinstance(files, list) or not 1 <= len(files) <= 8192:
        raise RuntimeError('Invalid bundled presentation file count')
    if not isinstance(revision, str) or not re.fullmatch('[A-Za-z0-9_.-]{1,96}', revision):
        raise RuntimeError('Invalid bundled presentation revision')
    targets = set()
    expected = {'manifest.json'}
    total = 0
    for item in files:
        if not isinstance(item, dict):
            raise RuntimeError('Invalid bundled presentation entry')
        asset, target = item.get('asset'), item.get('target')
        if not isinstance(asset, str) or not isinstance(target, str):
            raise RuntimeError('Invalid presentation asset/target name')
        target_path = PurePosixPath(target)
        if (target.startswith('/') or '\\' in target or target_path.as_posix() != target
                or any(p in ('', '.', '..') for p in target.split('/'))
                or len(target_path.parts) < 2 or target_path.parts[0] not in PRESENTATION_TARGETS):
            raise RuntimeError('Unsafe presentation target path')
        if target_path.suffix.lower() not in PRESENTATION_SUFFIXES:
            raise RuntimeError('Unapproved presentation format: ' + target)
        if asset != 'presentation/files/' + target or target in targets:
            raise RuntimeError('Unexpected or duplicate presentation asset mapping')
        size, digest = item.get('bytes'), item.get('sha256')
        if (type(size) is not int or not 0 < size <= 128 * 1024 * 1024
                or not isinstance(digest, str) or not re.fullmatch('[a-f0-9]{64}', digest)):
            raise RuntimeError('Invalid presentation size/hash metadata')
        relative = 'files/' + target
        path = root / relative
        if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
            raise RuntimeError('Presentation symlink/path escape')
        if not path.is_file() or path.stat().st_size != size:
            raise RuntimeError('Presentation file missing or wrong size: ' + target)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError('Presentation file hash mismatch: ' + target)
        suffix = target_path.suffix.lower()
        if suffix == '.png' and not data.startswith(b'\x89PNG\r\n\x1a\n'):
            raise RuntimeError('Invalid PNG presentation payload')
        if suffix in ('.jpg', '.jpeg') and not data.startswith(b'\xff\xd8\xff'):
            raise RuntimeError('Invalid JPEG presentation payload')
        if suffix == '.wav' and not (data.startswith(b'RIFF') and data[8:12] == b'WAVE'):
            raise RuntimeError('Invalid WAV presentation payload')
        if suffix == '.json':
            json.loads(data)
        if suffix == '.tsv':
            data.decode('utf-8-sig')
        if suffix in ('.json', '.tsv') and SECRETS.search(data):
            raise RuntimeError('Potential secret in presentation metadata')
        total += size
        if total > 768 * 1024 * 1024:
            raise RuntimeError('Presentation payload exceeds release limit')
        targets.add(target)
        expected.add(relative)
        entries['build/wiids-merge/assets/presentation/' + relative] = data
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    if actual != expected:
        raise RuntimeError('Presentation tree contains missing or unlisted files')
    entries['build/wiids-merge/assets/presentation/manifest.json'] = raw_manifest
    return {'revision': revision, 'manifest_sha256': hashlib.sha256(raw_manifest).hexdigest(),
            'files': len(files), 'bytes': total,
            'source': 'Exact owner-approved current presentation snapshot; attribution in WIIDS-NOTICE.md'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/release-source/WiiDS-source-candidate.zip')
    parser.add_argument('--check-presentation', action='store_true',
                        help='Validate the exact staged presentation only; do not create a source archive')
    args = parser.parse_args()
    if args.check_presentation:
        print(json.dumps(add_presentation({})))
        return
    output = args.output.resolve()
    if output.exists():
        raise SystemExit(f'Refusing to overwrite: {output}')
    entries = {}
    repos = []
    missing = []
    source_roots = []

    def add(name, path):
        if path.is_symlink() or not path.resolve().is_relative_to(ROOT):
            raise RuntimeError(f'Symlink or outside workspace: {name}')
        if path.stat().st_size > 16 * 1024 * 1024:
            raise RuntimeError(f'Unexpectedly large source (review required): {name}')
        data = path.read_bytes()
        if b'\0' in data:
            raise RuntimeError(f'Binary in source allowlist: {name}')
        try:
            data.decode('utf-8-sig')
        except UnicodeDecodeError:
            # Original third-party sources include legacy-encoded comments.
            # Preserve those bytes; still reject binary control characters.
            if not name.startswith('third_party/') or any(
                    b < 32 and b not in (9, 10, 12, 13) for b in data):
                raise RuntimeError(f'Non-UTF-8 source requires review: {name}') from None
        if SECRETS.search(data):
            raise RuntimeError(f'Potential secret in source (not printed): {name}')
        entries[name] = data

    for name in REPOS:
        repo = ROOT / 'reference' / name
        if not repo.is_dir():
            missing.append(name)
            continue
        metadata = ROOT / 'reference' / DERIVED.get(name, name)
        if Path(git(metadata, 'rev-parse', '--show-toplevel')).resolve() != metadata.resolve():
            raise RuntimeError(f'Metadata does not belong to selected checkout: {name}')
        revision = git(metadata, 'rev-parse', 'HEAD')
        remote = git(metadata, 'config', '--get', 'remote.origin.url')
        if not re.fullmatch(r'https://github\.com/[\w.-]+/[\w.-]+(?:\.git)?', remote):
            raise RuntimeError(f'Non-public or credential-bearing remote: {name}')
        submodules = {}
        for line in git(metadata, 'ls-files', '--stage').splitlines():
            if line.startswith('160000 '):
                header, subpath = line.split('\t', 1)
                submodules[subpath] = header.split()[1]
        repos.append({'directory': f'reference/{name}', 'upstream': remote, 'commit': revision,
                      'submodule_commits': submodules,
                      'source': 'working tree snapshot; generated outputs omitted'})
        for relative, path in candidates(repo):
            add(f'reference/{name}/{relative}', path)
        source_roots.append((f'reference/{name}', repo))
    for root_name in LOCAL_ROOTS:
        directory = ROOT / root_name
        if not directory.is_dir():
            missing.append(root_name)
            continue
        for relative, path in candidates(directory):
            add(f'{root_name}/{relative}', path)
        source_roots.append((root_name, directory))
    for target, (location, remote, revision) in DEPENDENCIES.items():
        directory = ROOT / location
        if not directory.is_dir():
            missing.append(target)
            continue
        if (directory / '.git').exists() and git(directory, 'rev-parse', 'HEAD') != revision:
            raise RuntimeError(f'Unexpected pinned dependency revision: {target}')
        repos.append({'directory': target, 'upstream': remote, 'commit_or_tag': revision,
                      'source': 'actual build dependency source, including applied fixups'})
        for relative, path in candidates(directory):
            add(f'{target}/{relative}', path)
        source_roots.append((target, directory))
    for name in ('LICENSE', 'THIRD-PARTY-NOTICES.md'):
        if (ROOT / name).is_file():
            add(name, ROOT / name)
    for name in EXTRA_SOURCE:
        if (ROOT / name).is_file():
            add(name, ROOT / name)
        else:
            missing.append(name)
    # Approved newly generated application branding is not extracted game art.
    logo = ROOT / 'build/branding/wiids-thor-minimal-v4.png'
    if logo.is_file():
        entries['build/branding/wiids-thor-minimal-v4.png'] = logo.read_bytes()
    # Generated shader identifiers/code-cache metadata from the pinned upstream,
    # not disc content. Keep the exact input for the integrated Galaxy APK.
    shader=ROOT/'reference/galaxy-android/assets/galaxy_shader_manifest.bin'
    entries['reference/galaxy-android/assets/galaxy_shader_manifest.bin']=shader.read_bytes()
    presentation = add_presentation(entries)
    coverage = {}
    omitted = []
    for prefix, directory in source_roots:
        count = 0
        for relative, path in expected_source_files(directory):
            name = f'{prefix}/{relative}'
            count += 1
            if name not in entries:
                omitted.append(name)
            elif entries[name] != path.read_bytes():
                raise RuntimeError(f'Source changed during export: {name}')
        coverage[prefix] = count
    if omitted:
        # Paths, never source contents, are reported so missing packages cannot
        # be silently accepted as complete preferred source.
        raise RuntimeError('Source coverage omissions: ' + json.dumps(omitted))
    manifest = {
        'format': 'wiids-source-candidate-v1', 'repositories': repos,
        'missing_roots': missing,
        'source_coverage': {'expected_source_files': sum(coverage.values()),
                            'roots': coverage, 'omissions': omitted},
        'presentation_assets': presentation,
        'excluded': 'ROMs; disc/game data; translated/generated game code; unlisted extracted assets; saves; keys; native binaries; private logs; other build trees',
        'limitations': [
            'Candidate requires review against the exact APK library/source inventory.',
            'Submodules and binary upstream build dependencies are fetched from pinned upstream sources.',
            'Generated game translations must be regenerated privately from an owner-supplied supported disc.',
            'No assertion of an independently verified clean-machine rebuild.',
        ],
        'files': {n: {'sha256': hashlib.sha256(d).hexdigest(), 'bytes': len(d)}
                  for n, d in sorted(entries.items())},
    }
    entries['SOURCE-MANIFEST.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    entries['README-SOURCE.md'] = (ROOT / 'docs/release/SOURCE-RELEASE.md').read_bytes()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + '.new')
    if temporary.exists():
        raise SystemExit(f'Refusing existing temporary output: {temporary}')
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, data in sorted(entries.items()):
                entry = zipfile.ZipInfo(name, date_time=(2026, 10, 20, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.external_attr = 0o100644 << 16
                archive.writestr(entry, data)
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip():
                raise RuntimeError('Source archive CRC verification failed')
            for name, data in entries.items():
                if archive.read(name) != data:
                    raise RuntimeError(f'Source archive readback mismatch: {name}')
        temporary.rename(output)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    print(json.dumps({'archive': str(output), 'sha256': digest, 'files': len(entries),
                      'missing_roots': missing,
                      'source_files_checked': sum(coverage.values()),
                      'source_coverage_omissions': omitted,
                      'status': 'source_candidate_not_publication_clearance'}))


if __name__ == '__main__':
    main()
