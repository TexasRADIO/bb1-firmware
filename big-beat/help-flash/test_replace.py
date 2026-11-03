#!/usr/bin/env python3
"""Host-only regression for guarded replacement of an existing BBTX block."""
import importlib.util
import struct
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('help_flash', HERE / 'help_flash.py')
hf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hf)

with tempfile.TemporaryDirectory(prefix='bbtx-replace-') as td:
    root = Path(td)
    base = bytearray(b'\xff' * hf.A_SIZE)
    base[:4] = b'BBSK'
    struct.pack_into('<I', base, 8, 0x100)
    p0, p1, p2 = root / 'base.bin', root / 'old.bin', root / 'new.bin'
    b0, b1 = root / 'old.bbtx', root / 'new.bbtx'
    p0.write_bytes(base)
    b0.write_bytes(hf.build_block([('TX_ONE', b'old help')])[0])
    b1.write_bytes(hf.build_block([('TX_ONE', b'new help'), ('TX_TWO', b'second line')])[0])

    hf.compose(p0, b0, p1)
    hf.compose(p1, b1, p2, replace=True)
    old, new = p1.read_bytes(), p2.read_bytes()
    rel = hf.FLASH_OFF - hf.A_BASE
    assert old[:rel] == new[:rel] and old[rel + hf.FLASH_MAX:] == new[rel + hf.FLASH_MAX:]
    assert new[rel:rel + len(b1.read_bytes())] == b1.read_bytes()
    assert set(new[rel + len(b1.read_bytes()):rel + hf.FLASH_MAX]) == {0xff}

    try:
        hf.compose(p1, b1, root / 'must-refuse.bin')
    except SystemExit as e:
        assert 'not erased' in str(e)
    else:
        raise AssertionError('compose accepted an occupied BBTX window')

    corrupt = root / 'corrupt.bin'
    bad = bytearray(old); bad[rel + 12] ^= 1; corrupt.write_bytes(bad)
    try:
        hf.compose(corrupt, b1, root / 'must-also-refuse.bin', replace=True)
    except SystemExit as e:
        assert 'CRC' in str(e)
    else:
        raise AssertionError('replace accepted a corrupt existing BBTX block')

print('guarded BBTX replacement: PASS')
