"""Targeted revision-migration checks; no synthetic gameplay claims."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import prepare_e2 as prep

ROOT=Path(__file__).resolve().parents[2]


class RevisionTests(unittest.TestCase):
    def setUp(self):
        self.mapper=prep.AddressMap((ROOT/'build/nsmbw-thor/address-map.txt').read_text())

    def test_changed_save_gap_fails_closed(self):
        with self.assertRaises(ValueError): self.mapper.map(0x800CF220)

    def test_actual_e2_rel_bases(self):
        self.assertEqual([self.mapper.map(a) for a in prep.E1_BASES.values()],
                         [0x80768500,0x8076D6C0,0x809A2CE0,0x80B1C960])

    def test_sdk_and_small_data_do_not_shift(self):
        for address in (0x801ED3C0,0x8042F680,0x80433080,0x80354E60):
            self.assertEqual(self.mapper.map(address),address)

    def test_real_preparation_receipt_and_review(self):
        folder=ROOT/'build/nsmbw-thor/e2-prepared'
        receipt=json.loads((folder/'preparation.json').read_text())
        audit=json.loads((folder/'runtime-e2-audit.json').read_text())
        self.assertEqual(receipt['map']['dropped_e1_seeds'],[])
        self.assertEqual(len(audit['changes']),19)
        self.assertEqual(audit['unmapped'],[])
        self.assertTrue(any(r['old']=='0x80808081' for r in audit['excluded']))
        self.assertFalse(any(r['old']=='0x80808081' for r in audit['changes']))
        self.assertEqual(prep.sha((folder/'game/main.dol').read_bytes()),prep.E2_DOL_SHA256)

    def test_correspondence_boundaries_are_explicit(self):
        self.assertEqual(self.mapper.map(0x807AAA30),0x807AAA70)
        with self.assertRaises(ValueError): self.mapper.map(0x807AAA31)
        self.assertEqual(self.mapper.map(0x807AAA6C),0x807AAA7C)

    def test_header_has_default_e1_and_opt_in_e2(self):
        header=(ROOT/'reference/nsmbw-android/runtime/include/nsmbw_disc_revision.h').read_text()
        self.assertEqual(header,prep.REVISION_HEADER)
        self.assertIn('#if defined(NSMBW_DISC_REVISION_E2) && NSMBW_DISC_REVISION_E2',header)
        self.assertIn('return e1;',header)


if __name__=='__main__': unittest.main()
