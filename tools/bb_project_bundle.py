#!/usr/bin/env python3
"""Create and verify deterministic, dependency-bound BB1 project bundles.

The native project remains the firmware's BBP2 wire-format family.  This
container adds release dependency identity; it is not a second serializer.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import tempfile
from typing import Optional, Tuple
import zipfile
import zlib

FORMAT = "BB1_PROJECT_BUNDLE"
SCHEMA = 1
PROJECT_PATH = "project.bbp2"
MANIFEST_PATH = "manifest.json"
MAX_PROJECT = 32768
MAX_ENTRIES = 8
MAX_BUNDLE_BYTES = 20 * 1024 * 1024
ZIP_TIME = (1980, 1, 1, 0, 0, 0)


class BundleError(ValueError):
    pass


def require(ok: bool, message: str) -> None:
    if not ok:
        raise BundleError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode()


def file_record(path: str, role: str, data: bytes) -> dict:
    return {"path": path, "role": role, "bytes": len(data), "sha256": digest(data)}


def parse_project(data: bytes) -> dict:
    require(64 <= len(data) <= MAX_PROJECT, "BBP2 size outside 64..32768")
    require(data[:4] == b"BBP2", "not a BBP2 project")
    version, reserved, header, reserved2 = struct.unpack_from("<BBBB", data, 4)
    require(version in (1, 2, 3) and not reserved and header == 64 and not reserved2, "unsupported BBP2 header")
    size, crc, features = struct.unpack_from("<III", data, 8)
    require(size == len(data) and features == version, "BBP2 size/features mismatch")
    require(not any(data[56:64]), "BBP2 reserved header bytes are nonzero")
    require(zlib.crc32(data[:12] + data[16:]) == crc, "BBP2 checksum mismatch")
    catalogue_crc, catalogue_stamp, library_header_crc, library_payload_crc, scene_bytes = struct.unpack_from("<IIIII", data, 20)
    sections, rows, references, working_parts = data[40:44]
    require(catalogue_crc and library_header_crc and library_payload_crc, "BBP2 dependency pins are zero")
    require(0 < scene_bytes <= 3840 and scene_bytes <= len(data) - 64, "BBP2 nested scene bounds")
    require(sections <= 32 and rows <= 64 and references <= 96 and working_parts <= 16, "BBP2 count bounds")
    return {
        "wire_version": version,
        "bytes": len(data),
        "sha256": digest(data),
        "crc32": f"{crc:08x}",
        "catalogue_crc32": f"{catalogue_crc:08x}",
        "catalogue_stamp": catalogue_stamp,
        "library_header_crc32": f"{library_header_crc:08x}",
        "library_payload_crc32": f"{library_payload_crc:08x}",
        "scene_bytes": scene_bytes,
        "counts": {"sections": sections, "rows": rows, "references": references, "working_parts": working_parts},
    }


def library_crcs(data: bytes) -> tuple[int, int]:
    require(len(data) >= 20, "sound library is too short")
    payload, header = struct.unpack_from("<II", data, 12)
    require(header and payload, "sound library identity CRC is zero")
    return header, payload


def safe_name(name: str) -> None:
    p = PurePosixPath(name)
    require(name and not name.startswith("/") and "\\" not in name, "unsafe bundle path")
    require(p.as_posix() == name and not any(x in ("", ".", "..") for x in p.parts), "unsafe bundle path")


def compiled_catalogue(source: Path) -> Tuple[bytes, dict]:
    text = (source / "firmware/src/bb/bb_performance_catalogue.inc").read_text()
    require("{" in text and "};" in text, "compiled catalogue source framing")
    payload = text.split("{", 1)[1].split("};", 1)[0]
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", payload))
    require(len(data) >= 64 and data[:4] == b"BBPC", "compiled catalogue framing")
    size, crc = struct.unpack_from("<II", data, 8)
    require(size == len(data) and zlib.crc32(data[:12] + data[16:]) == crc, "compiled catalogue checksum")
    return data, {"compiled_bytes": len(data), "compiled_sha256": digest(data),
                  "runtime_crc32": f"{crc:08x}", "runtime_stamp": 0x42504331}


def source_identity(source: Path) -> tuple[dict, dict, bytes, dict]:
    identity = json.loads((source / "big-beat/identity.json").read_text())
    spec = json.loads((source / "big-beat/sound-library.json").read_text())
    catalogue = (source / "big-beat/catalogue/catalogue.json").read_bytes()
    for key in ("display_name", "product", "version", "usb_product"):
        require(isinstance(identity.get(key), str) and identity[key], f"identity missing {key}")
    _compiled, runtime = compiled_catalogue(source)
    return {k: identity[k] for k in ("display_name", "product", "version", "usb_product")}, spec, catalogue, runtime


def dependency(path: Path, embedded: Optional[str], role: str) -> Tuple[dict, Optional[Tuple[str, str, bytes]]]:
    data = path.read_bytes()
    out = {"bytes": len(data), "sha256": digest(data), "embedded_path": embedded}
    return out, ((embedded, role, data) if embedded else None)


def create_bundle(project: Path, output: Path, source: Path, app: Path, package: Path,
                  library: Path, embed_app: bool = False, embed_package: bool = False,
                  embed_library: bool = False, embed_catalogue: bool = False) -> dict:
    require(not output.exists(), "output already exists")
    identity, library_spec, catalogue, runtime_catalogue = source_identity(source)
    project_data = project.read_bytes()
    project_meta = parse_project(project_data)
    require(project_meta["catalogue_crc32"] == runtime_catalogue["runtime_crc32"] and
            project_meta["catalogue_stamp"] == runtime_catalogue["runtime_stamp"], "project/compiled catalogue identity mismatch")
    library_data = library.read_bytes()
    require(len(library_data) == library_spec["bytes"] and digest(library_data) == library_spec["sha256"], "wrong source sound library")
    lib_header, lib_payload = library_crcs(library_data)
    require(project_meta["library_header_crc32"] == f"{lib_header:08x}" and
            project_meta["library_payload_crc32"] == f"{lib_payload:08x}", "project/library identity mismatch")

    entries: list[tuple[str, str, bytes]] = [(PROJECT_PATH, "project", project_data)]
    app_rec, item = dependency(app, "dependencies/app.bin" if embed_app else None, "application")
    if item: entries.append(item)
    pkg_rec, item = dependency(package, "dependencies/release.fwsc" if embed_package else None, "release_package")
    if item: entries.append(item)
    lib_rec, item = dependency(library, "dependencies/sound-library.bin" if embed_library else None, "sound_library")
    if item: entries.append(item)
    catalogue_path = "dependencies/catalogue.json" if embed_catalogue else None
    catalogue_rec = {"bytes": len(catalogue), "sha256": digest(catalogue), "embedded_path": catalogue_path, **runtime_catalogue}
    if catalogue_path: entries.append((catalogue_path, "catalogue_source", catalogue))
    lib_rec |= {"name": library_spec["name"], "header_crc32": f"{lib_header:08x}", "payload_crc32": f"{lib_payload:08x}"}

    records = [file_record(path, role, data) for path, role, data in sorted(entries)]
    manifest = {
        "format": FORMAT,
        "schema": SCHEMA,
        "identity": identity,
        "project": {"path": PROJECT_PATH, **project_meta},
        "dependencies": {
            "application": app_rec,
            "release_package": pkg_rec,
            "sound_library": lib_rec,
            "catalogue_source": catalogue_rec,
        },
        "entries": records,
    }
    manifest_data = canonical_json(manifest)
    all_entries = [(MANIFEST_PATH, "manifest", manifest_data), *entries]
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{output.name}.", suffix=".tmp", dir=output.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_STORED, strict_timestamps=True) as z:
            for name, _role, data in sorted(all_entries):
                info = zipfile.ZipInfo(name, ZIP_TIME)
                info.compress_type = zipfile.ZIP_STORED
                info.external_attr = 0o100644 << 16
                info.create_system = 3
                z.writestr(info, data)
        with open(temporary, "rb") as f:
            os.fsync(f.fileno())
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)
    return {"path": str(output.resolve()), "bytes": output.stat().st_size, "sha256": digest(output.read_bytes()), "manifest": manifest}


def _read_zip(bundle: Path) -> tuple[dict, dict[str, bytes], bytes]:
    require(bundle.is_file(), "bundle missing")
    raw = bundle.read_bytes()
    require(len(raw) <= MAX_BUNDLE_BYTES + 65536, "bundle file size limit")
    try:
        with zipfile.ZipFile(io.BytesIO(raw)) as z:
            infos = z.infolist()
            require(1 <= len(infos) <= MAX_ENTRIES, "bundle entry count")
            names = [i.filename for i in infos]
            require(len(names) == len(set(names)), "duplicate bundle path")
            for info in infos:
                safe_name(info.filename)
                require(not (info.flag_bits & 1), "encrypted bundle entry")
                require(not info.is_dir(), "directory entries are not allowed")
            require(sum(i.file_size for i in infos) <= MAX_BUNDLE_BYTES, "bundle expanded size limit")
            require(MANIFEST_PATH in names, "manifest missing")
            files = {i.filename: z.read(i) for i in infos}
    except (zipfile.BadZipFile, EOFError, OSError) as e:
        raise BundleError(f"invalid or incomplete bundle: {e}") from e
    try:
        manifest = json.loads(files[MANIFEST_PATH])
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        raise BundleError(f"invalid manifest: {e}") from e
    require(canonical_json(manifest) == files[MANIFEST_PATH], "manifest is not canonical JSON")
    return manifest, files, raw


def _match_file(record: dict, data: bytes, label: str) -> None:
    require(set(record) >= {"bytes", "sha256", "embedded_path"}, f"{label} record")
    require(record["bytes"] == len(data) and record["sha256"] == digest(data), f"{label} identity mismatch")


def verify_bundle(bundle: Path, source: Optional[Path] = None, app: Optional[Path] = None,
                  package: Optional[Path] = None, library: Optional[Path] = None,
                  require_self_contained: bool = False) -> dict:
    manifest, files, raw = _read_zip(bundle)
    require(set(manifest) == {"format", "schema", "identity", "project", "dependencies", "entries"}, "manifest fields")
    require(manifest["format"] == FORMAT and manifest["schema"] == SCHEMA, "bundle format/schema")
    require(isinstance(manifest["entries"], list), "entry table")
    records = manifest["entries"]
    require(len(records) == len(files) - 1, "entry table length")
    by_path = {}
    for record in records:
        require(set(record) == {"path", "role", "bytes", "sha256"}, "entry record fields")
        safe_name(record["path"])
        require(record["path"] != MANIFEST_PATH and record["path"] not in by_path, "duplicate manifest entry")
        require(record["path"] in files, "manifest names missing entry")
        _match_file({**record, "embedded_path": record["path"]}, files[record["path"]], record["path"])
        by_path[record["path"]] = record
    require(set(files) == {MANIFEST_PATH, *by_path}, "unlisted bundle entry")
    require(PROJECT_PATH in files and by_path[PROJECT_PATH]["role"] == "project", "project entry missing")
    project = parse_project(files[PROJECT_PATH])
    require(manifest["project"] == {"path": PROJECT_PATH, **project}, "project manifest mismatch")

    deps = manifest["dependencies"]
    require(set(deps) == {"application", "release_package", "sound_library", "catalogue_source"}, "dependency table")
    supplied = {"application": app, "release_package": package, "sound_library": library}
    verified = {}
    for name, path in supplied.items():
        record = deps[name]
        embedded = record.get("embedded_path")
        if embedded is not None:
            require(embedded in files, f"embedded {name} missing")
            require(by_path[embedded]["role"] == name, f"embedded {name} role mismatch")
            _match_file(record, files[embedded], name)
        if path is not None:
            _match_file(record, path.read_bytes(), name)
            verified[name] = "external-and-embedded" if embedded else "external"
        elif embedded:
            verified[name] = "embedded"
        else:
            verified[name] = "pinned-only"
    if require_self_contained:
        require(deps["release_package"].get("embedded_path") and deps["sound_library"].get("embedded_path"),
                "self-contained bundle requires package and sound library")

    lib_record = deps["sound_library"]
    lib_data = files.get(lib_record.get("embedded_path")) if lib_record.get("embedded_path") else (library.read_bytes() if library else None)
    if lib_data is not None:
        header, payload = library_crcs(lib_data)
        require(lib_record["header_crc32"] == f"{header:08x}" and lib_record["payload_crc32"] == f"{payload:08x}", "library CRC manifest")
        require(project["library_header_crc32"] == f"{header:08x}" and project["library_payload_crc32"] == f"{payload:08x}", "project/library CRC mismatch")

    if source is not None:
        identity, spec, catalogue, runtime_catalogue = source_identity(source)
        require(manifest["identity"] == identity, "release identity mismatch")
        _match_file(deps["catalogue_source"], catalogue, "catalogue source")
        for key, value in runtime_catalogue.items():
            require(deps["catalogue_source"].get(key) == value, "compiled catalogue identity mismatch")
        require(project["catalogue_crc32"] == runtime_catalogue["runtime_crc32"] and
                project["catalogue_stamp"] == runtime_catalogue["runtime_stamp"], "project/compiled catalogue mismatch")
        require(lib_record["name"] == spec["name"] and lib_record["bytes"] == spec["bytes"] and lib_record["sha256"] == spec["sha256"], "source library metadata mismatch")
        embedded = deps["catalogue_source"].get("embedded_path")
        if embedded:
            require(embedded in files, "embedded catalogue missing")
            require(by_path[embedded]["role"] == "catalogue_source", "embedded catalogue role mismatch")
            _match_file(deps["catalogue_source"], files[embedded], "catalogue source")
        verified["source"] = "identity-catalogue-library-metadata"
    return {
        "valid": True,
        "bundle_sha256": digest(raw),
        "bundle_bytes": len(raw),
        "project": project,
        "verified_dependencies": verified,
        "self_contained": bool(deps["release_package"].get("embedded_path") and deps["sound_library"].get("embedded_path")),
    }


def extract_project(bundle: Path, output: Path, **verify_args) -> dict:
    report = verify_bundle(bundle, **verify_args)
    _manifest, files, raw = _read_zip(bundle)
    require(digest(raw) == report["bundle_sha256"], "bundle changed during verification")
    data = files[PROJECT_PATH]
    require(len(data) <= MAX_PROJECT, "project capacity")
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{output.name}.", suffix=".tmp", dir=output.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data); f.flush(); os.fsync(f.fileno())
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)
    return report | {"extracted": str(output.resolve())}


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    c = sub.add_parser("create")
    for name in ("project", "output", "source", "app", "package", "library"):
        c.add_argument(f"--{name}", type=Path, required=True)
    c.add_argument("--embed-app", action="store_true")
    c.add_argument("--embed-package", action="store_true")
    c.add_argument("--embed-library", action="store_true")
    c.add_argument("--embed-catalogue", action="store_true")
    v = sub.add_parser("verify")
    v.add_argument("bundle", type=Path)
    for name in ("source", "app", "package", "library"):
        v.add_argument(f"--{name}", type=Path)
    v.add_argument("--require-self-contained", action="store_true")
    e = sub.add_parser("extract-project")
    e.add_argument("bundle", type=Path); e.add_argument("output", type=Path)
    for name in ("source", "app", "package", "library"):
        e.add_argument(f"--{name}", type=Path)
    e.add_argument("--require-self-contained", action="store_true")
    a = p.parse_args()
    if a.command == "create":
        result = create_bundle(a.project, a.output, a.source, a.app, a.package, a.library,
                               a.embed_app, a.embed_package, a.embed_library, a.embed_catalogue)
    else:
        kw = {k: getattr(a, k) for k in ("source", "app", "package", "library", "require_self_contained")}
        result = verify_bundle(a.bundle, **kw) if a.command == "verify" else extract_project(a.bundle, a.output, **kw)
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (BundleError, OSError, KeyError, TypeError, zipfile.BadZipFile) as e:
        raise SystemExit(f"project bundle refused: {e}")
