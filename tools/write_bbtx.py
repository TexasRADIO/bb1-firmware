#!/usr/bin/env python3
"""Write the BB1 BBTX text block to the FM-1's flash via USB-MIDI SysEx.

Uses the same OTA editor protocol as the web updater: the running BB1
firmware accepts SysEx write commands for specific flash regions.

  write_bbtx.py BB_TEXT_BLOCK.bin [--port NAME]

The block is written to flash offset 0xD7000 (region A + 0x37000).
The firmware must be running (not in UBOOT recovery mode).
Requires mido with python-rtmidi.
"""
import argparse, sys, time
try:
    import mido
except ImportError:
    sys.exit("write_bbtx: needs mido (pip install mido python-rtmidi)")

FLASH_OFF = 0xD7000
FLASH_MAX = 0x2000  # two sectors
MAGIC = b"BBTX"
SYSEX_PREFIX = [0x00, 0x22, 0x24, 0x35]
OTA_WRITE_CMD = 0x10   # editor flash-write sub-command
OTA_ERASE_CMD = 0x11   # editor flash-erase sub-command
BLOCK_SIZE = 256       # bytes per SysEx write
PORT_RE = None

def find_port(hint=None):
    import re
    names = mido.get_output_names()
    for n in names:
        if hint and hint.lower() in n.lower():
            return n
        if re.search(r'fm-1|big beat|felucca|sinco|usb-midi|midi', n, re.I):
            return n
    if names:
        return names[0]
    return None

def pack7(data):
    """Pack 8-bit bytes into 7-bit MIDI data bytes."""
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb > 0:
        out.append(acc & 0x7F)
    return out

def make_write_sysex(offset, data):
    """Build a SysEx message to write data at a flash offset."""
    # Format: F0 00 22 24 35 10 [offset 4 bytes packed7] [length 2 bytes packed7] [data packed7] F7
    payload = list(offset.to_bytes(4, 'little')) + list(len(data).to_bytes(2, 'little')) + list(data)
    packed = pack7(bytes(payload))
    return mido.Message('sysex', data=SYSEX_PREFIX + [OTA_WRITE_CMD] + packed)

def make_erase_sysex(offset, size):
    """Build a SysEx message to erase flash sectors."""
    payload = list(offset.to_bytes(4, 'little')) + list(size.to_bytes(4, 'little'))
    packed = pack7(bytes(payload))
    return mido.Message('sysex', data=SYSEX_PREFIX + [OTA_ERASE_CMD] + packed)

def main():
    parser = argparse.ArgumentParser(description="Write BBTX block to FM-1 flash")
    parser.add_argument("block", help="Path to bb_text_block.bin")
    parser.add_argument("--port", help="MIDI port name (substring match)")
    parser.add_argument("--dry-run", action="store_true", help="Show what would be sent")
    args = parser.parse_args()

    data = open(args.block, "rb").read()
    if not data.startswith(MAGIC):
        sys.exit(f"write_bbtx: {args.block}: not a BBTX block (missing magic)")
    if len(data) > FLASH_MAX:
        sys.exit(f"write_bbtx: block is {len(data)} bytes, max {FLASH_MAX}")

    print(f"block: {args.block} ({len(data)} bytes)")
    print(f"target: flash 0x{FLASH_OFF:X}..0x{FLASH_OFF + len(data):X}")

    if args.dry_run:
        n_blocks = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE
        print(f"would send: 1 erase + {n_blocks} writes")
        return

    port_name = find_port(args.port)
    if not port_name:
        sys.exit("write_bbtx: no MIDI output port found. Is the FM-1 connected?")
    print(f"port: {port_name}")

    out = mido.open_output(port_name)

    # Step 1: erase the two sectors at 0xD7000
    print("erasing 0x{:X}..0x{:X}".format(FLASH_OFF, FLASH_OFF + FLASH_MAX))
    out.send(make_erase_sysex(FLASH_OFF, FLASH_MAX))
    time.sleep(0.5)

    # Step 2: write in 256-byte blocks
    written = 0
    for off in range(0, len(data), BLOCK_SIZE):
        chunk = data[off:off + BLOCK_SIZE]
        out.send(make_write_sysex(FLASH_OFF + off, chunk))
        written += len(chunk)
        pct = written * 100 // len(data)
        sys.stdout.write(f"\rwriting {pct}%")
        sys.stdout.flush()
        time.sleep(0.02)  # don't overwhelm the USB

    print(f"\rwriting 100% ({written} bytes)")
    print("done — restart the FM-1 to load the new text block")
    out.close()

if __name__ == "__main__":
    main()
