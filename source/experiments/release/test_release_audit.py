"""Positive and negative publication-scanner fixtures (no owner data)."""
import importlib.util
from pathlib import Path
import random
import tempfile
import unittest
import zipfile

SPEC = importlib.util.spec_from_file_location('release_audit', Path(__file__).with_name('audit_release_apk.py'))
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class ReleaseAuditTests(unittest.TestCase):
    def check_apk(self, entries, sources=()):
        with tempfile.TemporaryDirectory(prefix='wiids-audit-fixture-') as folder:
            apk = Path(folder) / 'fixture.apk'
            with zipfile.ZipFile(apk, 'w') as archive:
                for name, data in {'lib/arm64-v8a/libapp.so': b'host runtime', **entries}.items():
                    archive.writestr(name, data)
            return AUDIT.audit(apk, sources)

    def test_host_runtime_and_art_allowed(self):
        self.assertEqual(self.check_apk({'assets/launcher/logo.png': b'own logo'})['status'], 'pass')

    def test_rom_extension_rejected(self):
        self.assertEqual(self.check_apk({'assets/disc/game.iso': b'ROM'})['status'], 'failed')

    def test_renamed_rom_magic_rejected(self):
        header = bytearray(0x40)
        header[0x18:0x1C] = b'\x5d\x1c\x9e\xa3'
        report = self.check_apk({'assets/hidden.bin': header})
        self.assertTrue(any('Disc image header' in item for item in report['findings']))

    def test_saved_profile_rejected(self):
        self.assertEqual(self.check_apk({'assets/saves/card-a/game.dat': b'private'})['status'], 'failed')

    def test_signing_key_rejected(self):
        self.assertEqual(self.check_apk({'assets/wiids-release.jks': b'key'})['status'], 'failed')

    def test_all_native_libraries_scanned(self):
        source = random.Random(42).randbytes(4096)
        report = self.check_apk({'lib/arm64-v8a/libsms_eclipse.so': b'ELF' + source}, [('fixture DOL', source)])
        self.assertEqual(report['status'], 'failed')
        self.assertEqual(report['source_window_checks'][0]['hits'][0]['library'], 'lib/arm64-v8a/libsms_eclipse.so')

    def test_unrecognized_mod_payload_rejected(self):
        report = self.check_apk({'assets/mods/eclipse.xdelta': b'not the pinned patch'})
        self.assertTrue(any('Unapproved bundled mod patch' in item for item in report['findings']))

    def test_unrecognized_nested_archive_rejected(self):
        report = self.check_apk({'assets/private.7z': b'uninspected'})
        self.assertEqual(report['status'], 'failed')

    def test_extracted_game_asset_rejected(self):
        self.assertEqual(self.check_apk({'assets/retail/Race.szs': b'private game pixels'})['status'], 'failed')


if __name__ == '__main__':
    unittest.main()
