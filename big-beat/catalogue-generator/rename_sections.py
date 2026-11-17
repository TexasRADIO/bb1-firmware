#!/usr/bin/env python3
"""Apply the reviewed named-song section labels to JSON and the target BBPC.

The map is exact: a source label must either occur once in the named song or
name an explicit part index when duplicate source labels exist.  Already-
renamed entries are accepted so the generator is safely repeatable.  Both the
public catalogue and stripped firmware catalogue are decoded and checked so
the only reader-visible differences are the mapped `parts[].name` strings.
Roland preset shelves are intentionally outside the map.
"""
from copy import deepcopy
from pathlib import Path
import hashlib
import json
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import bbpc  # noqa: E402


def sha(data):
    return hashlib.sha256(data).hexdigest()


def apply_map(value, mapping, label):
    out = deepcopy(value)
    songs = {song["song"]: song for song in out["songs"]}
    changed = []
    for row in mapping:
        if row["song"] not in songs:
            raise ValueError(f'{label}: missing song {row["song"]!r}')
        parts = songs[row["song"]]["parts"]
        if "part" in row:
            n = row["part"]
            if not isinstance(n, int) or not 0 <= n < len(parts):
                raise ValueError(f'{label}: {row["song"]!r} has invalid part index {n!r}')
            current = parts[n].get("name")
            if current == row["to"]:
                continue
            if current != row["from"]:
                raise ValueError(
                    f'{label}: {row["song"]!r} part {n} is {current!r}, expected '
                    f'{row["from"]!r} or {row["to"]!r}'
                )
        else:
            hits = [n for n, part in enumerate(parts) if part.get("name") == row["from"]]
            renamed = [n for n, part in enumerate(parts) if part.get("name") == row["to"]]
            if not hits and len(renamed) == 1:
                continue
            if len(hits) != 1:
                raise ValueError(f'{label}: {row["song"]!r} / {row["from"]!r} has {len(hits)} matches')
            n = hits[0]
        parts[n]["name"] = row["to"]
        changed.append({"song": row["song"], "part": n, "from": row["from"], "to": row["to"]})
    return out, changed


def differences(a, b, path=""):
    if type(a) is not type(b):
        return [path]
    if isinstance(a, dict):
        if list(a) != list(b):
            return [path + ".<keys>"]
        out = []
        for key in a:
            out += differences(a[key], b[key], f"{path}.{key}")
        return out
    if isinstance(a, list):
        if len(a) != len(b):
            return [path + ".<length>"]
        out = []
        for n, (left, right) in enumerate(zip(a, b)):
            out += differences(left, right, f"{path}[{n}]")
        return out
    return [] if a == b else [path]


def expected_paths(value, changed):
    song_index = {song["song"]: n for n, song in enumerate(value["songs"])}
    return sorted(f'.songs[{song_index[row["song"]]}].parts[{row["part"]}].name' for row in changed)


def main():
    tree = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    report_path = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    mapping = json.loads((HERE / "section-name-map.json").read_text())
    if len(mapping) != len({(row["song"], row.get("part"), row["from"]) for row in mapping}):
        raise ValueError("duplicate section-name mapping")

    public_path = tree / "big-beat/catalogue/catalogue.json"
    inc_path = tree / "firmware/src/bb/bb_performance_catalogue.inc"
    public_before = json.loads(public_path.read_text())
    blob_before = bbpc.read_inc(inc_path)
    target_before = bbpc.decode(blob_before)

    public_after, public_changes = apply_map(public_before, mapping, "public")
    target_after, target_changes = apply_map(target_before, mapping, "target")
    if public_changes != target_changes:
        raise ValueError("public and target catalogue part order differs")
    want_public = expected_paths(public_before, public_changes)
    want_target = expected_paths(target_before, target_changes)
    if sorted(differences(public_before, public_after)) != want_public:
        raise ValueError("public catalogue changed outside audited names")
    if sorted(differences(target_before, target_after)) != want_target:
        raise ValueError("target catalogue changed outside audited names")

    blob_after, encoding = bbpc.encode(target_after, version=3)
    if bbpc.decode(blob_after) != target_after:
        raise ValueError("renamed target catalogue does not round-trip")

    public_path.write_text(json.dumps(public_after, indent=2, ensure_ascii=False) + "\n")
    inc_path.write_text(bbpc.inc_text(
        blob_after,
        "BBPC v3: fixed-Huffman 768-byte metadata pages; only firmware-read fields kept. "
        "Named-song section labels normalized by catalogue-generator/section-name-map.json; musical data unchanged.",
    ))

    report = {
        "changes": public_changes,
        "changed_names": len(public_changes),
        "public_only_name_paths": want_public,
        "target_only_name_paths": want_target,
        "target_before": {"bytes": len(blob_before), "sha256": sha(blob_before)},
        "target_after": {"bytes": len(blob_after), "sha256": sha(blob_after)},
        "encoding": encoding,
    }
    if report_path:
        report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({
        "changed_names": report["changed_names"],
        "before_bytes": len(blob_before),
        "after_bytes": len(blob_after),
        "after_sha256": sha(blob_after),
    }))


if __name__ == "__main__":
    main()
