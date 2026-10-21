"""Host-only archive/argument checks; fixture bytes never enter Android app data."""
import hashlib
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import stage_disc


class OptionalMetadataStaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def test_missing_or_directory_source_rejected_before_device(self):
        for source in (self.root / 'absent.bca', self.root):
            with self.subTest(source=source), patch.object(sys, 'argv', ['stage_disc.py', '--disc', str(self.root / 'unused'), '--serial', 'unit-test-only', '--bca', str(source)]), patch.object(stage_disc.subprocess, 'run', side_effect=AssertionError('No device action allowed')), patch.object(stage_disc.subprocess, 'check_output', side_effect=AssertionError('No device action allowed')):
                with self.assertRaises(SystemExit): stage_disc.main()

    def test_invalid_sizes_rejected_before_device(self):
        source = self.root / 'wrong-size.bca'
        for size in (0, 63, 65, 128):
            source.write_bytes(b'fixture' * (size // 7) + b'x' * (size % 7))
            with self.subTest(size=size), patch.object(sys, 'argv', ['stage_disc.py', '--disc', str(self.root / 'unused'), '--serial', 'unit-test-only', '--bca', str(source)]), patch.object(stage_disc.subprocess, 'run', side_effect=AssertionError('No device action allowed')), patch.object(stage_disc.subprocess, 'check_output', side_effect=AssertionError('No device action allowed')):
                with self.assertRaises(SystemExit): stage_disc.main()

    def disc_fixture(self):
        disc = self.root / 'disc'
        (disc / 'files').mkdir(parents=True)
        (disc / 'sys').mkdir()
        (disc / 'files/test.txt').write_bytes(b'host-only fixture')
        (disc / 'sys/test.txt').write_bytes(b'another fixture')
        return disc

    def test_supplied_bytes_copied_verbatim_outside_disc(self):
        disc = self.disc_fixture()
        # Deliberately meaningless test bytes, never staged on a device.
        fixture = bytes(range(64))
        source = self.root / 'external-fixture.bca'
        source.write_bytes(fixture)
        manifest, archive = self.root / 'sha256.txt', self.root / 'disc.tar'
        self.assertEqual(stage_disc.write_archive(disc, source, manifest, archive), 3)
        expected = hashlib.sha256(fixture).hexdigest() + '  disc/bca.bin\n'
        self.assertIn(expected, manifest.read_text())
        with tarfile.open(archive) as bundle:
            self.assertEqual(bundle.extractfile('disc/bca.bin').read(), fixture)
            self.assertEqual(bundle.extractfile('.thor-sha256').read(), manifest.read_bytes())
        self.assertEqual(source.read_bytes(), fixture)

    def test_omitted_metadata_preserves_existing_tree_only(self):
        disc = self.disc_fixture()
        manifest, archive = self.root / 'sha256.txt', self.root / 'disc.tar'
        self.assertEqual(stage_disc.write_archive(disc, None, manifest, archive), 2)
        with tarfile.open(archive) as bundle:
            self.assertEqual(set(bundle.getnames()), {'files/test.txt', 'sys/test.txt', '.thor-sha256'})
        self.assertNotIn('bca', manifest.read_text())


if __name__ == '__main__': unittest.main()
