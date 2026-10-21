"""Copy the WiiDS notice and each app's licence/notice files into its APK assets (assets/licenses/).

Run before packaging; the files ship inside every APK so the licences travel with the binaries.
"""
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / 'reference'
NOTICE = ROOT / 'release/licenses/WIIDS-NOTICE.md'
DISC = ROOT / 'wiids/disc'

APPS = {
    'driftdroid/android/app': {
        'LICENSE-GPL-3.0.txt': REF / 'driftdroid/LICENSE',
        'THIRD-PARTY-NOTICES-DriftDroid.md': REF / 'driftdroid/THIRD-PARTY-NOTICES.md',
        'THIRD-PARTY-NOTICES-wiids_disc.md': DISC / 'THIRD-PARTY-NOTICES.md',
        'LICENSE-BSD-zstd.txt': DISC / 'third_party/zstd/LICENSE',
        'LICENSE-bzip2.txt': DISC / 'third_party/bzip2/LICENSE',
        'COPYING-liblzma.txt': DISC / 'third_party/liblzma/COPYING',
        'THIRD-PARTY-NOTICES-wiids_art.md': ROOT / 'wiids/art/THIRD-PARTY-NOTICES.md',
        # The other games now ship inside this one APK.
        'THIRD-PARTY-NOTICES-NSMBW.md': REF / 'nsmbw-android-e1/THIRD-PARTY-NOTICES.md',
        'LICENSE-MIT-sms-launcher.txt': REF / 'sms-launcher-thor/LICENSE',
        'LICENSE-MIT-sms-pc-port.txt': REF / 'sms-eclipse-thor/LICENSE',
        'LICENSE-CC0-sms-decomp.txt': REF / 'sms-thor/decomp/LICENSE',
        'LICENSE-zlib-SDL2.txt': ROOT / 'build/sunshine-thor/deps/SDL2-2.32.10/LICENSE.txt',
        'THIRD-PARTY-NOTICES-Eclipse.md': ROOT / 'release/licenses/THIRD-PARTY-NOTICES-Eclipse.md',
        'LICENSE-CC-BY-NC-ND-4.0.txt': ROOT / 'release/licenses/LICENSE-CC-BY-NC-ND-4.0.txt',
        'LICENSE-MIT-Eclipse-patcher.md': ROOT / 'release/licenses/LICENSE-MIT-Eclipse-patcher.md',
        'README-Eclipse-patcher.md': ROOT / 'release/licenses/README-Eclipse-patcher.md',
    },
    'nsmbw-android-e1/android/app': {
        'LICENSE-GPL-3.0.txt': REF / 'nsmbw-android-e1/LICENSE',
        'THIRD-PARTY-NOTICES-NSMBW.md': REF / 'nsmbw-android-e1/THIRD-PARTY-NOTICES.md',
    },
    'nsmbw-android-msmbwii/android/app': {
        'LICENSE-GPL-3.0.txt': REF / 'nsmbw-android-msmbwii/LICENSE',
        'THIRD-PARTY-NOTICES-NSMBW.md': REF / 'nsmbw-android-msmbwii/THIRD-PARTY-NOTICES.md',
    },
    'sms-launcher-thor/android/app': {
        'LICENSE-MIT-sms-launcher.txt': REF / 'sms-launcher-thor/LICENSE',
        'LICENSE-CC0-sms-decomp.txt': REF / 'sms-thor/decomp/LICENSE',
        'LICENSE-zlib-SDL2.txt': ROOT / 'build/sunshine-thor/deps/SDL2-2.32.10/LICENSE.txt',
    },
}


def main():
    for app, files in APPS.items():
        out = REF / app / 'src/main/assets/licenses'
        out.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(NOTICE, out / 'WIIDS-NOTICE.md')
        for name, source in files.items():
            shutil.copyfile(source, out / name)
        print(f'{app}: {1 + len(files)} files -> {out.relative_to(REF)}')


if __name__ == '__main__':
    main()
