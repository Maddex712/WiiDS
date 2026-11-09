"""Focused regression checks for the private source export's privacy boundaries."""
import importlib.util
from pathlib import Path
import json
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('private_export', Path(__file__).with_name('export_1_2_source.py'))
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


class SourceBoundaryTests(unittest.TestCase):
    def test_real_android_source_namespaces_are_retained(self):
        for path in ('app/src/main/kotlin/com/driftdroid/android/rom/WiiDsDisc.kt',
                     'app/src/main/java/com/driftdroid/android/saves/SaveArchive.java',
                     'runtime/src/game/save.cpp', 'runtime/src/cache/cache.cpp'):
            self.assertTrue(export.allowed(path), path)

    def test_private_and_generated_content_stays_out(self):
        for path in ('rom/disc.json', 'game/main.dol', 'wiicompiled/generated/ppc_func_123.c',
                     'runtime/data_sections_init.c', 'app/src/main/assets/presentation/manifest.json',
                     'app/src/main/jniLibs/arm64-v8a/libgame.so', '.git/config',
                     'translator/src/Cli/obj/Release/AssemblyInfo.cs', 'translator/src/Cli/bin/Release/deps.json',
                     'src/.env', 'src/keystore.properties', 'src/CODEX_HANDOFF.md'):
            self.assertFalse(export.allowed(path), path)

    def test_galaxy_decomp_exception_is_scoped(self):
        path = 'decomp/src/Game/Actor.cpp'
        self.assertFalse(export.allowed(path))
        self.assertTrue(export.allowed(path, galaxy=True))

    def test_source_extensions_and_notices(self):
        for path in ('CMakeLists.txt', 'LICENSE', 'src/engine.cpp', 'src/overlay.patch', 'recomp/config.yml'):
            self.assertTrue(export.allowed(path), path)
        self.assertFalse(export.allowed('screenshot.png'))
        self.assertFalse(export.allowed('game.iso'))

    def test_presentation_traversal_is_rejected_before_reading_media(self):
        parent = Path('M:/WiiDS-release-wrap-2026-11-08/package-review/privacy-test-fixtures').resolve()
        parent.mkdir(parents=True, exist_ok=True)
        for target in ('launcher/../../saves/card.png', '/launcher/x.png', 'launcher//x.png'):
            with self.subTest(target=target), tempfile.TemporaryDirectory(dir=parent) as temp:
                private = Path(temp).resolve()
                self.assertTrue(private.is_relative_to(parent))
                folder = private / 'android/app/src/main/assets/presentation'
                folder.mkdir(parents=True)
                data = json.dumps({'schema': 1, 'kind': 'wiids-bundled-presentation', 'revision': 'test',
                                   'files': [{'target': target, 'asset': 'presentation/files/' + target,
                                              'bytes': 1, 'sha256': '0' * 64}]}).encode()
                (folder / 'manifest.json').write_bytes(data)
                apk = private / 'fixture.zip'
                with zipfile.ZipFile(apk, 'w') as z:
                    z.writestr('assets/presentation/manifest.json', data)
                with self.assertRaisesRegex(ValueError, 'Unsafe, unapproved, or duplicate'):
                    export.approved_presentation(private, apk)


if __name__ == '__main__':
    unittest.main()
