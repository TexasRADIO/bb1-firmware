#!/usr/bin/env python3
"""Validate an r9 HANDOFF.json and the committed files it names."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
ARTIFACTS = {"application", "package", "sound_region", "rollback"}


def fail(message: str) -> None:
    raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def require_object(value, label: str) -> dict:
    if not isinstance(value, dict):
        fail(f"{label} must be an object")
    return value


def require_list(value, label: str) -> list:
    if not isinstance(value, list):
        fail(f"{label} must be an array")
    return value


def require_text(value, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        fail(f"{label} must be a non-empty string")
    return value


def require_hash(value, label: str, pattern=HEX64) -> str:
    value = require_text(value, label)
    if not pattern.fullmatch(value):
        fail(f"{label} has invalid hexadecimal identity")
    return value


def require_bytes(value, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        fail(f"{label} must be a positive integer")
    return value


def safe_path(value, label: str) -> str:
    value = require_text(value, label)
    pure = PurePosixPath(value)
    if pure.is_absolute() or ".." in pure.parts or value != pure.as_posix():
        fail(f"{label} must be a normalized relative POSIX path")
    if value in {".", ""}:
        fail(f"{label} must name a file")
    return value


def validate_record(record: object, label: str, *, with_bytes: bool) -> dict:
    record = require_object(record, label)
    safe_path(record.get("path"), f"{label}.path")
    require_hash(record.get("sha256"), f"{label}.sha256")
    if with_bytes:
        require_bytes(record.get("bytes"), f"{label}.bytes")
    return record


def validate(data: object) -> list[str]:
    data = require_object(data, "top level")
    if data.get("schema") != 1:
        fail("schema must be 1")
    if data.get("candidate") != "r9":
        fail("candidate must be r9")

    base = require_object(data.get("base"), "base")
    require_text(base.get("repository"), "base.repository")
    require_hash(base.get("commit"), "base.commit", HEX40)

    source = require_object(data.get("source"), "source")
    safe_path(source.get("inventory"), "source.inventory")
    require_hash(source.get("inventory_sha256"), "source.inventory_sha256")
    require_hash(source.get("expected_tree_sha256"), "source.expected_tree_sha256")

    recipe = validate_record(data.get("recipe"), "recipe", with_bytes=False)
    results = validate_record(data.get("results"), "results", with_bytes=False)
    resources = validate_record(data.get("resources"), "resources", with_bytes=False)

    patches = require_list(data.get("patches"), "patches")
    for index, patch in enumerate(patches, start=1):
        patch = validate_record(patch, f"patches[{index - 1}]", with_bytes=False)
        if patch.get("order") != index:
            fail("patch order must be consecutive integers starting at 1")

    inputs = require_list(data.get("inputs"), "inputs")
    names = set()
    for index, item in enumerate(inputs):
        item = validate_record(item, f"inputs[{index}]", with_bytes=True)
        name = require_text(item.get("name"), f"inputs[{index}].name")
        if name in names:
            fail(f"duplicate input name: {name}")
        names.add(name)

    artifacts = require_object(data.get("artifacts"), "artifacts")
    missing = sorted(ARTIFACTS - set(artifacts))
    extra = sorted(set(artifacts) - ARTIFACTS)
    if missing:
        fail("missing artifacts: " + ", ".join(missing))
    if extra:
        fail("unknown artifacts: " + ", ".join(extra))
    for name in sorted(ARTIFACTS):
        validate_record(artifacts[name], f"artifacts.{name}", with_bytes=True)

    committed = [
        source["inventory"],
        recipe["path"],
        results["path"],
        resources["path"],
        *(patch["path"] for patch in patches),
        *(item["path"] for item in inputs),
    ]
    if len(committed) != len(set(committed)):
        fail("committed file paths must be unique")
    return committed


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_files(data: dict, root: Path, committed: list[str], check_git: bool) -> None:
    expected = {
        data["source"]["inventory"]: data["source"]["inventory_sha256"],
        data["recipe"]["path"]: data["recipe"]["sha256"],
        data["results"]["path"]: data["results"]["sha256"],
        data["resources"]["path"]: data["resources"]["sha256"],
        **{entry["path"]: entry["sha256"] for entry in data["patches"]},
        **{entry["path"]: entry["sha256"] for entry in data["inputs"]},
    }
    root = root.resolve()
    for relative in committed:
        path = root / relative
        if path.is_symlink() or not path.is_file():
            fail(f"committed path is not a regular file: {relative}")
        if path.resolve().parent != root and root not in path.resolve().parents:
            fail(f"committed path escapes root: {relative}")
        actual = sha256(path)
        if actual != expected[relative]:
            fail(f"sha256 mismatch for {relative}: {actual}")
        if check_git:
            result = subprocess.run(
                ["git", "-C", str(root), "ls-files", "--error-unmatch", "--", relative],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode != 0:
                fail(f"committed path is not tracked by git: {relative}")

    if check_git:
        commit = data["base"]["commit"]
        result = subprocess.run(
            ["git", "-C", str(root), "cat-file", "-e", f"{commit}^{{commit}}"],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            fail(f"base commit is not available in this checkout: {commit}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--skip-git", action="store_true")
    args = parser.parse_args()
    try:
        data = json.loads(
            args.manifest.read_text(encoding="utf-8"), object_pairs_hook=unique_object
        )
        committed = validate(data)
        root = args.root or args.manifest.parent
        verify_files(data, root, committed, not args.skip_git)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"r9-handoff: INVALID: {exc}", file=sys.stderr)
        return 2
    print(
        f"r9-handoff: valid ({len(committed)} committed files, "
        f"{len(data['patches'])} ordered patches, {len(data['inputs'])} inputs)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
