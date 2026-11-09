"""Add the optional private Plus case to an already integrated WiiDS MKDD tree.

Usage: python -I apply_plus.py <android-tree> <private-Plus-library>
Anchors are validated before writing; repeat runs are accepted only for exact edits.
The standard native library and APK assets are never replaced by this overlay.
"""
from pathlib import Path
import importlib.util
import shutil
import sys
sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
KT = 'app/src/main/kotlin/com/driftdroid/android/'
ACTIVITY = '''        <activity android:name="dev.thor.mkdd.WiiDsMkddPlusGameActivity" android:exported="false" android:process=":mkdd_plus_game"
            android:launchMode="singleTask" android:screenOrientation="sensorLandscape" android:resizeableActivity="false"
            android:preferMinimalPostProcessing="true" android:theme="@android:style/Theme.Black.NoTitleBar.Fullscreen"
            android:configChanges="density|layoutDirection|locale|grammaticalGender|fontScale|fontWeightAdjustment|colorMode|orientation|uiMode|screenLayout|screenSize|smallestScreenSize|keyboard|keyboardHidden|navigation">
            <meta-data android:name="android.app.lib_name" android:value="wiids_mkdd_plus_game" />
        </activity>

'''
EDITS = {
    'app/src/main/AndroidManifest.xml': [
        ((ACTIVITY.replace(':mkdd_plus_game', ':mkdd_game') + '        <!-- New Super Mario Bros. Wii: launcher-side screens in ":nsmbw"',
          '        <!-- New Super Mario Bros. Wii: launcher-side screens in ":nsmbw"'),
         ACTIVITY + '        <!-- New Super Mario Bros. Wii: launcher-side screens in ":nsmbw"')],
    KT + 'GameStorage.kt': [
        (('            FrontendGame.MKDD -> { target("disc",outside,"mkdd/game"); target("art",outside,"mkdd/lower-art") }',
          '            FrontendGame.MKDD -> {\n'
          '                target("disc",outside,"mkdd/game"); target("art",outside,"mkdd/lower-art")\n'
          '                target("plus-disc",outside,"mkdd/plus/game")\n'
          '                target("plus-art",outside,"mkdd/plus/lower-art")\n'
          '                target("plus-overview",outside,"mkdd/plus/overview")\n'
          '            }'),
         '            FrontendGame.MKDD -> { target("disc",outside,"mkdd/game"); target("art",outside,"mkdd/lower-art") }\n'
         '            FrontendGame.MKDD_PLUS -> {\n'
         '                target("plus-disc",outside,"mkdd/plus/game")\n'
         '                target("plus-art",outside,"mkdd/plus/lower-art")\n'
         '                target("plus-overview",outside,"mkdd/plus/overview")\n'
         '            }'),
        ('        protected+=File(outside,"mkdd/saves")',
         '        protected+=File(outside,"mkdd/saves")\n'
         '        protected+=File(outside,"mkdd/save-backups")\n'
         '        protected+=File(outside,"mkdd/plus/saves")\n'
         '        protected+=File(outside,"mkdd/plus/save-backups")')]
}
spec = importlib.util.spec_from_file_location('plus_case_edits', HERE / 'plus_case_edits.py')
case = importlib.util.module_from_spec(spec)
spec.loader.exec_module(case)
for rel, edits in case.EDITS.items():
    EDITS.setdefault(rel, []).extend(edits)


def apply(root, lib):
    if root.is_relative_to(HERE.parents[2]) or lib.is_relative_to(HERE.parents[2]):
        raise ValueError('The integrated tree and library must remain private, outside the repo')
    if not lib.is_file() or lib.read_bytes()[:4] != b'\x7fELF':
        raise ValueError('Expected the private ARM64 Plus library')
    updated = {}
    for rel, edits in EDITS.items():
        path = root / rel
        raw = path.read_bytes().decode('utf-8')
        text = raw.replace('\r\n', '\n')
        for old, new in edits:
            if text.count(new) == 1:
                continue
            variants = (old,) if isinstance(old, str) else old
            matches = [v for v in variants if text.count(v) == 1]
            if not matches:
                raise ValueError(f'{rel}: expected exactly one anchor: {str(old)[:100]}')
            text = text.replace(matches[0], new)
        updated[path] = text.replace('\n', '\r\n') if '\r\n' in raw else text
    for path, text in updated.items():
        path.write_bytes(text.encode('utf-8'))
    java = root / 'app/src/main/java/dev/thor/mkdd'
    for source in sorted((HERE.parent / 'android/dev/thor/mkdd').glob('*.java')) + sorted((HERE / 'dev/thor/mkdd').glob('*.java')):
        shutil.copy2(source, java / source.name)
    shutil.copy2(lib, root / 'app/src/main/jniLibs/arm64-v8a/libwiids_mkdd_plus_game.so')
    print('Plus case applied; standard library, original save profile and content assets retained')


if __name__ == '__main__':
    apply(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
