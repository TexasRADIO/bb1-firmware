#!/usr/bin/env python3

import unittest

from rename_sections import apply_map


class RenameSectionsTests(unittest.TestCase):
    def test_repeatable_unique_and_indexed_mappings(self):
        catalogue = {
            "songs": [
                {"song": "Unique", "parts": [{"name": "Old"}]},
                {"song": "Duplicate", "parts": [{"name": "Part 1"}, {"name": "Part 1"}]},
            ]
        }
        mapping = [
            {"song": "Unique", "from": "Old", "to": "New"},
            {"song": "Duplicate", "part": 0, "from": "Part 1", "to": "Part 1A"},
            {"song": "Duplicate", "part": 1, "from": "Part 1", "to": "Part 1B"},
        ]
        renamed, changes = apply_map(catalogue, mapping, "test")
        self.assertEqual([part["name"] for part in renamed["songs"][1]["parts"]], ["Part 1A", "Part 1B"])
        self.assertEqual(len(changes), 3)
        repeated, repeated_changes = apply_map(renamed, mapping, "test")
        self.assertEqual(repeated, renamed)
        self.assertEqual(repeated_changes, [])

    def test_indexed_mapping_rejects_unexpected_source(self):
        catalogue = {"songs": [{"song": "Example", "parts": [{"name": "Something Else"}]}]}
        with self.assertRaisesRegex(ValueError, "expected"):
            apply_map(catalogue, [{"song": "Example", "part": 0, "from": "Old", "to": "New"}], "test")


if __name__ == "__main__":
    unittest.main()
