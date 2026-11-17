#!/usr/bin/env python3
"""Build BB1's self-contained Firmware Hub package from pinned release bytes.

The released application package remains the source of the application flash
image.  This tool extends that verified image to one MiB, inserts the matching
BB1 sound library at its fixed window, and replaces the package loader with the
BB1 loader that writes exactly the application and library windows.  It never
opens USB or contacts a device.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1pkg_make as pack  # type: ignore
import lz4blk  # type: ignore

BASE_FLASH_BYTES = 0x93000
FLASH_BYTES = 0x100000
SOUND_OFFSET = 0xA0000
SOUND_BYTES = 0x3C000
SOUND_END = SOUND_OFFSET + SOUND_BYTES


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def decode(data: bytes, key: int = 0xFFFF) -> bytes:
    out = bytearray(data)
    pack.enc(out, 0, len(out), key)
    return bytes(out)


def unpack(package: Path) -> tuple[str, dict[str, bytes]]:
    raw = package.read_bytes()
    if len(raw) < 0x400:
        raise ValueError("package is too small")
    logical = bytearray()
    product = ""
    for index in range(20):
        logical += raw[index * 48:index * 48 + 47]
        marker = raw[index * 48 + 47]
        if marker != 0x7D:
            product += chr((marker - index - 1) & 0xFF)
    logical += raw[960:]
    header = decode(bytes(logical[:64]))
    if pack.crc16(header[2:]) != struct.unpack_from("<H", header)[0]:
        raise ValueError("package header CRC mismatch")
    total, count = struct.unpack_from("<IH", header, 4)
    if total != len(logical) or count != 2 or header[16:22] != b"AC791N":
        raise ValueError("unexpected package layout")
    encoded_entries = bytes(logical[64:64 + count * 80])
    if pack.crc16(encoded_entries) != struct.unpack_from("<H", header, 2)[0]:
        raise ValueError("package entry-list CRC mismatch")
    payload: dict[str, bytes] = {}
    for index in range(count):
        entry = decode(encoded_entries[index * 80:(index + 1) * 80])
        _, entry_index, checksum, _, offset, size, padded = struct.unpack_from("<HHHHIII", entry)
        name = entry[64:].split(b"\0", 1)[0].decode()
        data = bytes(logical[offset:offset + size])
        if entry_index != index or padded < size or len(data) != size or pack.crc16(data) != checksum:
            raise ValueError(f"invalid {name or 'unnamed'} package entry")
        payload[name] = data
    if set(payload) != {"flash.bin", "ota.bin"}:
        raise ValueError("package must contain flash.bin and ota.bin")
    return product, payload


def loader_payload(loader: bytes) -> bytes:
    if not 0 < len(loader) <= 0x14000 or b"FELUCCA-LOADER-1" not in loader:
        raise ValueError("loader is missing, too large, or not Felucca-derived")

    def header(crc: int, a: int, b: int, attr: int) -> bytes:
        body = struct.pack("<HIIBBH16s", crc, a, b, attr, 0, 0, b"usb_hid_ota.bin")
        return struct.pack("<H", pack.crc16(body)) + body

    blocks = bytearray()
    for offset in range(0, len(loader), 4096):
        raw = loader[offset:offset + 4096]
        compressed = lz4blk.compress(raw)
        if len(compressed) > 4096 or lz4blk.decompress(compressed) != raw:
            raise ValueError("loader compression verification failed")
        blocks += struct.pack("<II", len(compressed), len(raw)) + compressed
    body = header(pack.crc16(loader), len(loader), 0x01C0A800, 0) + blocks
    return header(pack.crc16(body), 0x20, len(body), 0x41) + body


def expected(path: Path, digest: str | None, label: str) -> bytes:
    data = path.read_bytes()
    if digest and sha(data) != digest.lower():
        raise ValueError(f"{label} SHA-256 mismatch")
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True, help="verified released BB1 application package")
    parser.add_argument("--sounds", type=Path, required=True, help="matching 240 KiB BB1 sound library")
    parser.add_argument("--loader", type=Path, required=True, help="BB1 full-install loader.bin")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-product", required=True)
    parser.add_argument("--expect-base-sha256")
    parser.add_argument("--expect-sounds-sha256")
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError(f"refusing to overwrite {args.output}")

    base_raw = expected(args.base, args.expect_base_sha256, "base package")
    sounds = expected(args.sounds, args.expect_sounds_sha256, "sound library")
    loader = args.loader.read_bytes()
    product, payload = unpack(args.base)
    if product != args.expect_product:
        raise ValueError(f"base product {product!r}, expected {args.expect_product!r}")
    if len(payload["flash.bin"]) != BASE_FLASH_BYTES:
        raise ValueError(f"base flash is {len(payload['flash.bin']):#x}, expected {BASE_FLASH_BYTES:#x}")
    if len(sounds) != SOUND_BYTES or sounds[:4] != b"BBSK":
        raise ValueError("sound library is not BB1's 240 KiB BBSK image")

    flash = bytearray(payload["flash.bin"])
    flash += b"\xFF" * (FLASH_BYTES - len(flash))
    if any(byte != 0xFF for byte in flash[SOUND_OFFSET:SOUND_END]):
        raise ValueError("base package's BB1 sound window is not blank")
    flash[SOUND_OFFSET:SOUND_END] = sounds
    package = pack.ufw(bytes(flash), loader_payload(loader), product)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    result = {
        "schema": "bb1-firmware-hub-package/v1",
        "file": args.output.name,
        "bytes": len(package),
        "sha256": sha(package),
        "product": product,
        "base_file": args.base.name,
        "base_sha256": sha(base_raw),
        "sounds_file": args.sounds.name,
        "sounds_sha256": sha(sounds),
        "loader_sha256": sha(loader),
        "flash_bytes": len(flash),
        "write_ranges": [["0x4000", "0x93000"], ["0xA0000", "0xDC000"]],
        "device_contacted": False,
    }
    Path(str(args.output) + ".json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
