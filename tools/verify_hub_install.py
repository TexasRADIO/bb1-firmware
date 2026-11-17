#!/usr/bin/env python3
"""Verify BB1's full Firmware Hub package against its exact released inputs."""
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
from package_hub_install import (BASE_FLASH_BYTES, FLASH_BYTES, SOUND_BYTES,
                                 SOUND_END, SOUND_OFFSET, sha, unpack)


def loader_from(ota: bytes) -> bytes:
    def header(blob: bytes) -> tuple[int, int, int, int]:
        hc, dc, a, b, attr, _, _, name = struct.unpack("<HHIIBBH16s", blob[:32])
        if pack.crc16(blob[2:32]) != hc or name != b"usb_hid_ota.bin\0":
            raise ValueError("invalid loader header")
        return dc, a, b, attr

    checksum, offset, length, attr = header(ota)
    if offset != 32 or length != len(ota) - 32 or attr != 0x41 or pack.crc16(ota[32:]) != checksum:
        raise ValueError("invalid outer loader payload")
    checksum, length, address, attr = header(ota[32:])
    if address != 0x01C0A800 or attr != 0 or not 0 < length <= 0x14000:
        raise ValueError("invalid inner loader payload")
    at = 64
    loader = bytearray()
    while at < len(ota):
        compressed, plain = struct.unpack_from("<II", ota, at)
        at += 8
        if not 0 < compressed <= 4096 or not 0 < plain <= 4096 or at + compressed > len(ota):
            raise ValueError("invalid loader block")
        block = lz4blk.decompress(ota[at:at + compressed])
        if len(block) != plain:
            raise ValueError("loader block length mismatch")
        loader += block
        at += compressed
    if len(loader) != length or pack.crc16(loader) != checksum:
        raise ValueError("loader length or CRC mismatch")
    return bytes(loader)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--sounds", type=Path, required=True)
    parser.add_argument("--loader", type=Path, required=True)
    parser.add_argument("--product", required=True)
    args = parser.parse_args()

    meta = json.loads(Path(str(args.package) + ".json").read_text())
    raw = args.package.read_bytes()
    if sha(raw) != meta["sha256"]:
        raise ValueError("package metadata hash mismatch")
    product, payload = unpack(args.package)
    base_product, base_payload = unpack(args.base)
    sounds = args.sounds.read_bytes()
    loader = loader_from(payload["ota.bin"])
    expected_loader = args.loader.read_bytes()
    flash = payload["flash.bin"]
    if product != base_product or product != args.product or product != meta["product"]:
        raise ValueError("product identity mismatch")
    if len(flash) != FLASH_BYTES or len(base_payload["flash.bin"]) != BASE_FLASH_BYTES:
        raise ValueError("flash image length mismatch")
    if flash[:BASE_FLASH_BYTES] != base_payload["flash.bin"]:
        raise ValueError("released application flash changed")
    if len(sounds) != SOUND_BYTES or flash[SOUND_OFFSET:SOUND_END] != sounds:
        raise ValueError("matching sound library is not exact in the package")
    if any(byte != 0xFF for byte in flash[BASE_FLASH_BYTES:SOUND_OFFSET]):
        raise ValueError("unexpected bytes before the sound library")
    if any(byte != 0xFF for byte in flash[SOUND_END:]):
        raise ValueError("unexpected bytes after the sound library")
    if loader != expected_loader or sha(loader) != meta["loader_sha256"]:
        raise ValueError("package loader is not the compiled BB1 full-install loader")
    if b"FELUCCA-LOADER-1" not in loader:
        raise ValueError("Felucca loader marker missing")
    result = {
        "passed": True,
        "product": product,
        "package_sha256": sha(raw),
        "base_application_flash_exact": True,
        "sounds_sha256": hashlib.sha256(sounds).hexdigest(),
        "sounds_exact": True,
        "loader_sha256": sha(loader),
        "loader_exact": True,
        "all_container_crcs": True,
        "bytes_outside_application_and_library_blank": True,
        "write_ranges": meta["write_ranges"],
        "device_contacted": False,
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
