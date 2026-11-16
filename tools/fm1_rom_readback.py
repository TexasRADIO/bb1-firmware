#!/usr/bin/env python3
"""Capture two full flash reads through one directly connected FM-1 ROM endpoint."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path

RESCUE_SHA256 = "fc8687107535049b7ac7a7257eb63ae571c75049d0c64bd5f23beba167e7fbaa"
ROM_ID = (0x4C4A, 0x8057)
FLASH_BYTES = 0x100000


def sha(data):
    return hashlib.sha256(data).hexdigest()


def capture(device, loader, output):
    vendor, product = device.inquiry()
    if (vendor, product) != ("WL82", "UBOOT1.00"):
        raise ValueError(f"Unexpected ROM SCSI identity: {vendor} {product}")
    device.start_loader(loader)
    key, flash_type, flash_id = device.chip()
    if (key, flash_type, flash_id) != (0x980F, 3, 0x856014):
        raise ValueError(f"Unqualified chip: key {key:04X}, type {flash_type}, ID {flash_id:06X}")
    hashes = []
    for number in (1, 2):
        data = device.read(0, FLASH_BYTES)
        full = len(data) == FLASH_BYTES
        path = output / f"flash-{number}.{'bin' if full else 'partial'}"
        with path.open("xb") as file:
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        digest = sha(data)
        if sha(path.read_bytes()) != digest:
            raise ValueError(f"Saved flash read {number} differs from captured bytes")
        with path.with_suffix(path.suffix + ".sha256").open("x") as file:
            file.write(f"{digest}  {path.name}\n")
            file.flush()
            os.fsync(file.fileno())
        if not full:
            raise ValueError(f"Flash read {number} was short ({len(data)} bytes)")
        hashes.append(digest)
    if (output / "flash-1.bin").read_bytes() != (output / "flash-2.bin").read_bytes():
        raise ValueError("Independent reads differ. Both captures were retained.")
    return {"scsi": f"{vendor} {product}", "flash_id": f"{flash_id:06X}",
            "read_bytes": FLASH_BYTES, "read_sha256": hashes, "flash_written": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rescue_source", type=Path, help="pinned fm1_rescue.py")
    parser.add_argument("loader", type=Path, help="local pinned wl82loader.bin")
    parser.add_argument("output", type=Path, nargs="?", help="new directory for the two captures")
    parser.add_argument("--check-tools", action="store_true",
                        help="verify local dependencies without enumerating USB devices")
    args = parser.parse_args()
    if args.check_tools == (args.output is not None):
        parser.error("supply either --check-tools or a new capture output directory")
    if sha(args.rescue_source.read_bytes()) != RESCUE_SHA256:
        parser.error("recovery source hash differs")
    spec = importlib.util.spec_from_file_location("fm1_rescue", args.rescue_source)
    rescue = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rescue)
    loader = rescue.get_loader(args.loader)
    import usb.core
    import usb.util
    backend = rescue.backend(usb)
    if args.check_tools:
        print(json.dumps(dict(status="PASS", rescue_sha256=RESCUE_SHA256,
                              loader_sha256=sha(loader), loader_bytes=len(loader),
                              libusb_backend_loaded=True, usb_enumeration_performed=False)))
        return
    devices = list(usb.core.find(find_all=True, idVendor=ROM_ID[0], idProduct=ROM_ID[1],
                                 backend=backend))
    if len(devices) != 1:
        parser.error(f"expected one direct 4C4A:8057 ROM device, found {len(devices)}")
    device = devices[0]
    args.output.mkdir(mode=0o700)
    try:
        try:
            if device.is_kernel_driver_active(0):
                device.detach_kernel_driver(0)
        except NotImplementedError:
            pass
        result = capture(rescue.Uboot(rescue.Bot(device, usb)), loader, args.output)
        result.update(usb_id="4C4A:8057", usb_bus=device.bus, usb_address=device.address)
        (args.output / "READBACK.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result))
    finally:
        usb.util.dispose_resources(device)


if __name__ == "__main__":
    main()
