#!/usr/bin/env python3

import unittest

from audit_section_names import audit


class SectionNameAuditTests(unittest.TestCase):
    def test_objective_and_review_findings_are_separated(self):
        report = audit({
            "songs": [
                {
                    "song": "Example",
                    "parts": [
                        {"name": "Part1"},
                        {"name": "1"},
                        {"name": "Part 2 + Snare"},
                        {"name": "Same"},
                        {"name": "Same"},
                    ],
                },
                {
                    "song": "Roland CR-8000",
                    "parts": [{"name": "Hand Clap"}],
                },
            ]
        })
        self.assertEqual(report["scope"]["named_songs"], 1)
        self.assertEqual(report["scope"]["sections"], 5)
        self.assertEqual(report["summary"]["by_kind"]["number_only"], 1)
        self.assertEqual(report["summary"]["by_kind"]["compact_part_number"], 1)
        self.assertEqual(report["summary"]["by_kind"]["drum_voice_language"], 1)
        self.assertEqual(report["summary"]["by_kind"]["duplicate_within_song"], 2)
        self.assertNotIn("Hand Clap", [item["label"] for item in report["findings"]])


if __name__ == "__main__":
    unittest.main()
