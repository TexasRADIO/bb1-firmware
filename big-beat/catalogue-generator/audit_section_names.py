#!/usr/bin/env python3
"""Report named-song section labels that need editorial or musical review.

This is intentionally read-only.  It identifies objective hygiene defects and
potentially unclear labels without inventing verse, chorus or bridge roles.
The four Roland preset shelves are excluded because their voice and style names
are historically meaningful machine labels, not song sections.
"""

from collections import Counter
from pathlib import Path
import argparse
import json
import re


PRESET_SHELVES = {
    "Roland CR-8000",
    "Roland CR-78",
    "Roland TR-66",
    "Roland TR-77",
}

VOICE_WORDS = re.compile(r"\b(?:kick|snare|rim(?:shot)?|claps?|toms?|hi-?hats?|hats?)\b", re.I)
COMPACT_PART = re.compile(r"^Part\d", re.I)
NUMBER_ONLY = re.compile(r"^\d+$")
GENERIC_PART = re.compile(r"^Part\s*\d+(?:\s*[+·&/]\s*\d+|[A-Z])?$", re.I)


def finding(song, index, label, kind, severity, detail):
    return {
        "song": song,
        "part_index": index,
        "label": label,
        "kind": kind,
        "severity": severity,
        "detail": detail,
    }


def audit(catalogue):
    findings = []
    songs = [song for song in catalogue["songs"] if song["song"] not in PRESET_SHELVES]
    for song in songs:
        song_name = song["song"]
        labels = [part.get("name", "") for part in song["parts"]]
        duplicates = {label for label, count in Counter(labels).items() if label and count > 1}
        for index, label in enumerate(labels):
            if label != label.strip():
                findings.append(finding(song_name, index, label, "outer_whitespace", "error", "Leading or trailing whitespace."))
            if label in duplicates:
                findings.append(finding(song_name, index, label, "duplicate_within_song", "error", "The same visible label identifies more than one section."))
            if COMPACT_PART.search(label):
                findings.append(finding(song_name, index, label, "compact_part_number", "warning", "Use a space after Part for consistent scanning."))
            if NUMBER_ONLY.fullmatch(label):
                findings.append(finding(song_name, index, label, "number_only", "error", "A number alone does not explain the section's role."))
            if VOICE_WORDS.search(label):
                findings.append(finding(song_name, index, label, "drum_voice_language", "review", "The label describes implementation or instrumentation rather than musical function."))
            if GENERIC_PART.fullmatch(label):
                findings.append(finding(song_name, index, label, "generic_part", "review", "Musical role remains unknown; retain until listening evidence supports a clearer name."))
    counts = Counter(item["kind"] for item in findings)
    severities = Counter(item["severity"] for item in findings)
    return {
        "scope": {
            "named_songs": len(songs),
            "preset_shelves_excluded": sorted(PRESET_SHELVES),
            "sections": sum(len(song["parts"]) for song in songs),
        },
        "policy": "Objective hygiene may be fixed mechanically. Musical roles require listening evidence; do not infer them from drum events or source labels.",
        "summary": {
            "findings": len(findings),
            "by_kind": dict(sorted(counts.items())),
            "by_severity": dict(sorted(severities.items())),
        },
        "findings": findings,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("catalogue", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = audit(json.loads(args.catalogue.read_text()))
    text = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    print(json.dumps(report["summary"], sort_keys=True))


if __name__ == "__main__":
    main()
