"""Fail-closed provenance checks for selecting E1 versus E2 native products."""
import copy
import unittest

from stage_native import DOLS, PIN, validate_cache, validate_receipts


class ProductSelection(unittest.TestCase):
    def receipts(self, revision):
        return ({'status': 'built', 'disc_revision': revision, 'source_commit': PIN,
                 'dol_sha256': DOLS[revision]},
                {'status': 'translation_artifacts_verified_not_gameplay', 'revision': revision,
                 'source_commit': PIN, 'dol_sha256': DOLS[revision]})

    def test_matching_revisions(self):
        for revision in (1, 2):
            validate_receipts(*self.receipts(revision), revision)

    def test_cross_revision_receipts_are_rejected(self):
        for revision in (1, 2):
            other = 3 - revision
            with self.assertRaises(SystemExit):
                validate_receipts(*self.receipts(other), revision)
            with self.assertRaises(SystemExit):
                validate_receipts(self.receipts(revision)[0], self.receipts(other)[1], revision)

    def test_unverified_or_modified_inputs_are_rejected(self):
        for receipt_index, key, value in ((0, 'status', 'configured'), (0, 'source_commit', 'bad'),
                (0, 'dol_sha256', 'bad'), (1, 'status', 'translated'),
                (1, 'source_commit', 'bad'), (1, 'dol_sha256', 'bad')):
            receipts = copy.deepcopy(self.receipts(1))
            receipts[receipt_index][key] = value
            with self.assertRaises(SystemExit): validate_receipts(*receipts, 1)

    def test_old_e1_input_receipts_are_rejected(self):
        old = '6d755ec24c395fee600a856a85785e0e99c0b7ecb680d573a6cdd1aa6f7beee7'
        for receipt_index in (0, 1):
            receipts = copy.deepcopy(self.receipts(1))
            receipts[receipt_index]['dol_sha256'] = old
            with self.assertRaises(SystemExit): validate_receipts(*receipts, 1)

    def test_e2_profile_remains_original(self):
        self.assertEqual(DOLS[2], '0373b4dbffd7c9da9d27eb7257a7677877a3e33ceb81818bab1daa93560b9907')

    def test_cache_matches_revision(self):
        validate_cache('CMAKE_CXX_FLAGS:STRING=\nMKW_BUILD_PRODUCTS:BOOL=ON\n', 1)
        validate_cache('CMAKE_CXX_FLAGS:STRING=-DNSMBW_DISC_REVISION_E2=1\nMKW_BUILD_PRODUCTS:BOOL=ON\n', 2)

    def test_e2_definition_never_allowed_in_e1(self):
        for define in ('-DNSMBW_DISC_REVISION_E2=1', '-DNSMBW_DISC_REVISION_E2=0', '-DNSMBW_DISC_REVISION_E2'):
            with self.assertRaises(SystemExit):
                validate_cache(f'CMAKE_CXX_FLAGS:STRING={define}\nMKW_BUILD_PRODUCTS:BOOL=ON\n', 1)

    def test_missing_product_or_revision_definition_rejected(self):
        for cache in ('CMAKE_CXX_FLAGS:STRING=\nMKW_BUILD_PRODUCTS:BOOL=ON\n',
                      'CMAKE_CXX_FLAGS:STRING=-DNSMBW_DISC_REVISION_E2=0\nMKW_BUILD_PRODUCTS:BOOL=ON\n',
                      'CMAKE_CXX_FLAGS:STRING=-DNSMBW_DISC_REVISION_E2=1\nMKW_BUILD_PRODUCTS:BOOL=OFF\n'):
            with self.assertRaises(SystemExit): validate_cache(cache, 2)


if __name__ == '__main__': unittest.main()
