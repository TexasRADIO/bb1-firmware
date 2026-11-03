#!/usr/bin/env python3
"""Exercise the real library verifier and package rejection before output creation.
Usage: python3 library_identity.py VERIFIED_LIBRARY
"""
from pathlib import Path
import json, subprocess, sys, tempfile
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from bb_library_identity import verify_library
library = Path(sys.argv[1]).resolve()
checks = 0

def rejects(path, source, expected):
    global checks
    try:
        verify_library(path, source)
    except ValueError as e:
        assert expected in str(e), str(e)
    else:
        raise AssertionError('invalid library accepted')
    checks += 1

verified = verify_library(library, ROOT)
checks += 1
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    data = library.read_bytes()
    for name, payload in [('truncated', data[:-1]), ('corrupt', bytes([data[0] ^ 1]) + data[1:])]:
        bad = tmp / name
        bad.write_bytes(payload)
        rejects(bad, ROOT, 'wrong sound library')
        out = tmp / (name + '-package')
        result = subprocess.run([sys.executable, str(ROOT / 'big-beat/integration/package/package_candidate.py'),
                                 str(tmp / 'nonexistent-build'), str(out), 'test', str(bad)],
                                capture_output=True, text=True)
        assert result.returncode != 0 and 'wrong sound library' in result.stderr
        assert not out.exists()
        checks += 1
    source = tmp / 'source'
    (source / 'big-beat').mkdir(parents=True)
    pins = source / 'firmware/src/bb/bb_text_pin.h'
    pins.parent.mkdir(parents=True)
    spec = (ROOT / 'big-beat/sound-library.json').read_text()
    (source / 'big-beat/sound-library.json').write_text(spec)
    original = (ROOT / 'firmware/src/bb/bb_text_pin.h').read_text()
    pins.write_text(original.replace('BB_TX_PIN_COUNT 363u', 'BB_TX_PIN_COUNT 364u'))
    assert pins.read_text() != original
    rejects(library, source, 'help pins differ')
    pins.write_text(original)
    metadata = json.loads(spec)
    metadata['help_window_bytes'] = 32
    (source / 'big-beat/sound-library.json').write_text(json.dumps(metadata))
    rejects(library, source, 'help block bounds')
print(json.dumps({'checks': checks, 'passed': True, 'library': verified, 'device_contacted': False}, indent=2))
