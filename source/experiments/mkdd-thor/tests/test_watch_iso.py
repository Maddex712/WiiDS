"""Synthetic input tests: never reads or writes an owner's game image."""
import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('watch_iso',Path(__file__).resolve().parents[1]/'watch_iso.py')
watch=importlib.util.module_from_spec(spec)
spec.loader.exec_module(watch)


class InputChecks(unittest.TestCase):
    def setUp(self):
        # Deliberately tiny authored fixture; production constants are restored.
        self.temp=tempfile.TemporaryDirectory(dir=os.environ['TEMP'],prefix='mkdd-input-test-')
        self.folder=Path(self.temp.name)
        self.old=(watch.DISC_BYTES,watch.DISC_SHA1,watch.DOL_SHA1)
        self.image=bytearray(8192)
        self.image[:6]=b'GM4E01'
        self.image[0x1c:0x20]=bytes.fromhex('c2339f3d')
        struct.pack_into('>I',self.image,0x420,0x800)
        dol=bytearray(0x104)
        struct.pack_into('>I',dol,0,0x100)
        struct.pack_into('>I',dol,0x90,4)
        dol[0x100:]=b'TEST'
        self.image[0x800:0x904]=dol
        watch.DISC_BYTES=len(self.image)
        watch.DISC_SHA1=hashlib.sha1(self.image).hexdigest()
        watch.DOL_SHA1=hashlib.sha1(dol).hexdigest()

    def tearDown(self):
        watch.DISC_BYTES,watch.DISC_SHA1,watch.DOL_SHA1=self.old
        self.temp.cleanup()

    def file(self,name='game.iso',data=None):
        path=self.folder/name
        path.write_bytes(self.image if data is None else data)
        return path

    def test_complete_accepts_both_identities(self):
        self.assertEqual(watch.inspect(self.file())['status'],'verified')

    def test_partial_extension_never_ready_even_at_complete_length(self):
        self.assertEqual(watch.inspect(self.file('game.crdownload'))['status'],'partial')

    def test_final_extension_does_not_rescue_truncation(self):
        self.assertEqual(watch.inspect(self.file(data=self.image[:-1]))['status'],'partial')

    def test_wrong_game_ignored(self):
        self.image[:6]=b'GM4P01'
        self.assertIsNone(watch.inspect(self.file()))

    def test_fake_header_rejected(self):
        self.image[0x1c]=0
        self.assertIsNone(watch.inspect(self.file()))

    def test_full_length_corruption_rejected(self):
        self.image[-1]=1
        self.assertEqual(watch.inspect(self.file())['status'],'identity_mismatch')

    def test_dol_independently_checked(self):
        watch.DOL_SHA1='0'*40
        self.assertEqual(watch.inspect(self.file())['status'],'identity_mismatch')

    def test_dol_overflow_stays_unaccepted(self):
        struct.pack_into('>I',self.image,0x800+0x90,0xffffffff)
        self.assertNotEqual(watch.inspect(self.file())['status'],'verified')

    def test_short_header_ignored(self):
        self.assertIsNone(watch.inspect(self.file(data=self.image[:20])))

    def test_missing_path_ignored(self):
        self.assertIsNone(watch.inspect(self.folder/'missing.iso'))

    def test_scan_separates_partial_from_valid(self):
        self.file('pending.part'); self.file('finished.gcm')
        self.assertEqual(sorted(x['status'] for x in watch.scan(self.folder)),['partial','verified'])


if __name__=='__main__':
    unittest.main()
