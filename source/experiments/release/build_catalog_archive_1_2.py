"""Rebuild the pinned archive JNI library privately, never staging it into an APK.

Default records/validates the build plan. --build verifies/extracts pinned upstream tarballs,
builds static XZ/libarchive and the JNI target, then records its hash. No app/device/publication.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import re
import tarfile

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('M:/WiiDS-mkdd-2026-11-01/all-game-packs-2026-11-04')
PINS = {'libarchive-3.8.11.tar.gz': '900fe9fc7237c2b04f090f3c3648de8fe597525771f5035016537ce2d667d12b',
        'xz-5.8.4.tar.gz': '0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9'}
URLS = {'libarchive-3.8.11.tar.gz': 'https://github.com/libarchive/libarchive/releases/download/v3.8.11/libarchive-3.8.11.tar.gz',
        'xz-5.8.4.tar.gz': 'https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.gz'}


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def extract(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(source) as archive:
        for item in archive:
            path = (destination / item.name).resolve()
            if destination.resolve() not in path.parents and path != destination.resolve():
                raise ValueError('Unsafe source tar member')
            if item.isdir():
                path.mkdir(parents=True, exist_ok=True)
            elif item.isfile():
                path.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(item) as stream:
                    path.write_bytes(stream.read())
            else:
                raise ValueError('Source tar links/devices require review: ' + item.name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--private-root', type=Path, default=Path('M:/WiiDS-release-wrap-2026-11-08'))
    p.add_argument('--downloads', type=Path, default=BASE / 'downloads')
    p.add_argument('--ndk', type=Path, default=ROOT / '.tools/android-sdk/ndk/27.2.12479018')
    p.add_argument('--cmake', type=Path, default=ROOT / '.tools/host-tools/cmake/data/bin/cmake.exe')
    p.add_argument('--ninja', type=Path, default=ROOT / '.tools/host-tools/bin/ninja.exe')
    p.add_argument('--jni-source', type=Path, help='Use the exported reference/driftdroid/android/app/src/main/cpp/wiids_catalog_archive.c')
    p.add_argument('--build', action='store_true')
    a = p.parse_args()
    out = a.private_root.resolve() / 'package-review/catalog-archive'
    out.mkdir(parents=True, exist_ok=True)
    for name, digest in PINS.items():
        if sha(a.downloads / name) != digest:
            raise ValueError('Pinned dependency tarball hash mismatch: ' + name)
    ndk = a.ndk.resolve()
    common = ['-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + a.ninja.resolve().as_posix(),
              '-DCMAKE_TOOLCHAIN_FILE=' + (ndk / 'build/cmake/android.toolchain.cmake').as_posix(),
              '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-26', '-DANDROID_STL=c++_static',
              '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
              '-DBUILD_SHARED_LIBS=OFF', '-DBUILD_TESTING=OFF']
    sources = out / 'sources'
    xz, archive = sources / 'xz-5.8.4', sources / 'libarchive-3.8.11'
    xb, ab, jb, prefix = (out / x for x in ('xz-build', 'archive-build', 'jni-build', 'prefix'))
    jni = (a.jni_source or a.private_root.resolve() / 'android/app/src/main/cpp/wiids_catalog_archive.c').resolve()
    commands = [
        [str(a.cmake), '-S', str(xz), '-B', str(xb), *common, '-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG',
         '-DCMAKE_INSTALL_PREFIX=' + prefix.as_posix(), '-DXZ_NLS=OFF', '-DXZ_TOOL_XZ=OFF',
         '-DXZ_TOOL_XZDEC=OFF', '-DXZ_TOOL_LZMADEC=OFF', '-DXZ_TOOL_LZMAINFO=OFF'],
        [str(a.cmake), '--build', str(xb), '--target', 'liblzma', '--parallel', '4'],
        [str(a.cmake), '--install', str(xb)],
        [str(a.cmake), '-S', str(archive), '-B', str(ab), *common, '-DCMAKE_C_FLAGS_RELEASE=',
         '-DENABLE_TEST=OFF', '-DENABLE_TAR=OFF', '-DENABLE_CPIO=OFF', '-DENABLE_CAT=OFF', '-DENABLE_UNZIP=OFF',
         '-DENABLE_ACL=OFF', '-DENABLE_XATTR=OFF', '-DENABLE_OPENSSL=OFF', '-DENABLE_NETTLE=OFF',
         '-DENABLE_MBEDTLS=OFF', '-DENABLE_LIBXML2=OFF', '-DENABLE_EXPAT=OFF', '-DENABLE_BZip2=OFF',
         '-DENABLE_LZ4=OFF', '-DENABLE_LZO=OFF', '-DENABLE_ZSTD=OFF', '-DENABLE_LZMA=ON', '-DENABLE_ZLIB=ON',
         '-DLIBLZMA_INCLUDE_DIR=' + (prefix / 'include').as_posix(),
         '-DLIBLZMA_LIBRARY=' + (prefix / 'lib/liblzma.a').as_posix()],
        [str(a.cmake), '--build', str(ab), '--target', 'archive_static', '--parallel', '4'],
        [str(a.cmake), '-S', str(Path(__file__).with_name('catalog-archive')), '-B', str(jb), *common,
         '-DWIIDS_ARCHIVE_SOURCE=' + archive.as_posix(), '-DWIIDS_ARCHIVE_LIBRARY=' + (ab / 'libarchive/libarchive.a').as_posix(),
         '-DWIIDS_LZMA_SOURCE=' + xz.as_posix(), '-DWIIDS_LZMA_LIBRARY=' + (prefix / 'lib/liblzma.a').as_posix(),
         '-DWIIDS_CATALOG_JNI=' + jni.as_posix()],
        [str(a.cmake), '--build', str(jb), '--target', 'wiids_catalog_archive', '--parallel', '4'],
    ]
    plan = {'dependency_tarball_sha256': PINS, 'dependency_urls': URLS, 'jni_sha256': sha(jni), 'commands': commands,
            'built': False, 'staged': False, 'installed': False, 'published': False}
    if a.build:
        for filename in PINS:
            target = sources / filename.removesuffix('.tar.gz')
            if not target.exists():
                extract(a.downloads / filename, sources)
        with (out / 'build.log').open('w', encoding='utf-8') as log:
            for command in commands:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        library = jb / 'libwiids_catalog_archive.so'
        strip = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
        final = out / 'libwiids_catalog_archive.so'
        subprocess.run([str(strip), '--strip-unneeded', '-o', str(final), str(library)], check=True)
        readelf = strip.with_name('llvm-readelf.exe')
        symbols = subprocess.check_output([str(readelf), '--dyn-syms', str(final)], text=True)
        headers = subprocess.check_output([str(readelf), '-l', str(final)], text=True)
        exports = sorted(set(re.findall(r'Java_com_driftdroid_android_CatalogArchive_\w+', symbols)))
        expected = ['Java_com_driftdroid_android_CatalogArchive_' + name for name in ('close', 'next', 'open', 'read')]
        if exports != expected or any(int(line.split()[-1], 16) < 16384 for line in headers.splitlines() if line.strip().startswith('LOAD ')):
            raise ValueError('Rebuilt archive JNI exports or Android page alignment are invalid')
        (out / 'readelf-symbols.txt').write_text(symbols, encoding='utf-8')
        (out / 'readelf-segments.txt').write_text(headers, encoding='utf-8')
        plan.update(built=True, library_sha256=sha(final), bytes=final.stat().st_size,
                    jni_exports=exports, load_alignment_16k=True,
                    existing_library_sha256=sha(a.private_root / 'android/app/src/main/jniLibs/arm64-v8a/libwiids_catalog_archive.so'))
    (out / 'build-plan.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in plan.items() if k != 'commands'}))


if __name__ == '__main__':
    main()
