"""Release boundary regressions: source namespaces and curated presentation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


def load(name):
    path = Path(__file__).with_name(name + '.py')
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


exporter = load('export_release_source')
merged = load('stage_merged')


class SourceBoundaryTests(unittest.TestCase):
    def test_handwritten_namespaces_are_source(self):
        for name in (
            'android/app/src/main/kotlin/com/driftdroid/android/rom/DiscInstallCommit.kt',
            'android/app/src/main/java/org/example/game/Saves.java',
            'runtime/src/game/save/cache/Engine.cpp',
            'runtime/include/cache/Cache.h',
            'src/game/Mario.cpp',
        ):
            with self.subTest(name=name):
                self.assertTrue(exporter.eligible(name))

    def test_private_roots_and_generated_data_are_not_source(self):
        for name in ('generated/functions.cpp', 'rom/sections.h',
                     'DiscData/sys/main.dol', 'decomp/src/game.cpp',
                     'android/app/build/generated/BuildConfig.java',
                     'android/native/assets/runtime/game.cfg',
                     'runtime/assets/font.txt', 'runtime/src/data_sections_runtime.cpp'):
            with self.subTest(name=name):
                self.assertFalse(exporter.eligible(name))

    def test_independent_coverage_retains_rom_package(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            good = root / 'android/app/src/main/kotlin/example/rom/Install.kt'
            bad = root / 'generated/translated.cpp'
            for path in (good, bad):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('// fixture\n')
            actual = {name for name, _ in exporter.expected_source_files(root)}
            self.assertEqual(actual, {good.relative_to(root).as_posix()})


class PresentationBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.presentation = self.root / 'build/wiids-merge/assets/presentation'
        self.presentation.mkdir(parents=True)
        self.patches = [patch.object(exporter, 'ROOT', self.root),
                        patch.object(exporter, 'PRESENTATION_ROOT', self.presentation)]
        for item in self.patches:
            item.start()
        self.data = b'{"kind":"test-artwork"}\n'
        self.payload = self.presentation / 'files/launcher/artwork.json'
        self.payload.parent.mkdir(parents=True)
        self.payload.write_bytes(self.data)
        self.manifest = {'schema': 1, 'kind': 'wiids-bundled-presentation',
                         'revision': 'thor-fixture-r1', 'files': [{
            'asset': 'presentation/files/launcher/artwork.json',
            'target': 'launcher/artwork.json', 'bytes': len(self.data),
            'sha256': hashlib.sha256(self.data).hexdigest()}]}
        self.write_manifest()

    def tearDown(self):
        for item in reversed(self.patches):
            item.stop()
        self.temporary.cleanup()

    def write_manifest(self):
        (self.presentation / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_exact_manifest_payload_included(self):
        entries = {}
        summary = exporter.add_presentation(entries)
        self.assertEqual(summary['files'], 1)
        self.assertEqual(entries['build/wiids-merge/assets/presentation/files/launcher/artwork.json'], self.data)
        self.assertEqual(len(entries), 2)

    def test_changed_payload_rejected(self):
        self.payload.write_bytes(b'X' * len(self.data))
        with self.assertRaisesRegex(RuntimeError, 'hash mismatch'):
            exporter.add_presentation({})

    def test_unlisted_rom_rejected(self):
        (self.presentation / 'private.iso').write_bytes(b'not-a-real-disc')
        with self.assertRaisesRegex(RuntimeError, 'unlisted'):
            exporter.add_presentation({})

    def test_traversal_rejected(self):
        self.manifest['files'][0]['target'] = 'launcher/../../private.json'
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, 'Unsafe'):
            exporter.add_presentation({})

    def test_unapproved_format_rejected(self):
        self.manifest['files'][0]['target'] = 'launcher/disc.iso'
        self.write_manifest()
        with self.assertRaisesRegex(RuntimeError, 'format'):
            exporter.add_presentation({})


class MergeStagingTests(unittest.TestCase):
    def test_owned_runtime_replaced_presentation_and_patch_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            assets = root / 'build/wiids-merge/assets'
            source = root / 'input-runtime'
            source.mkdir()
            (source / 'runtime.txt').write_text('new runtime')
            for name, data in (('presentation/manifest.json', 'presentation'),
                               ('mods/eclipse-v1.1.0.xdelta', 'patch'),
                               ('runtime-nsmbw/old.txt', 'old runtime')):
                path = assets / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(data)
            with patch.object(merged, 'ROOT', root), patch.object(merged, 'ASSETS', assets):
                merged.replace_runtime_assets('runtime-nsmbw', source)
            self.assertEqual((assets / 'runtime-nsmbw/runtime.txt').read_text(), 'new runtime')
            self.assertFalse((assets / 'runtime-nsmbw/old.txt').exists())
            self.assertEqual((assets / 'presentation/manifest.json').read_text(), 'presentation')
            self.assertEqual((assets / 'mods/eclipse-v1.1.0.xdelta').read_text(), 'patch')
            self.assertFalse(any(path.name.startswith('.runtime-') for path in assets.iterdir()))


if __name__ == '__main__':
    unittest.main()
