#!/usr/bin/env python3
"""Drop catalogue fields the firmware never reads, keeping the BBPC v3 encoding (flash-data study, 31 Oct 2026).

usage: python3 -I strip_unread.py TREE              rewrite TREE/firmware/src/bb/bb_performance_catalogue.inc
       python3 -I strip_unread.py TREE --check      only check firmware-keys.txt against the firmware (exit 1 on a gap)

firmware-keys.txt lists every field kept. --check scans TREE/firmware for C string literals; any catalogue field the
firmware names but the list lacks is an error, so a new reader cannot silently lose its data. The record tree, text
and order of every kept field are unchanged; bbpc.py's encoder (byte-identical on the unstripped catalogue) writes
the result and the script decodes it again to prove the round trip. No device access."""
import glob, hashlib, json, re, sys
from pathlib import Path
tree = Path(sys.argv[1]); here = Path(__file__).resolve().parent
sys.path.insert(0, str(here)); import bbpc
inc = tree / 'firmware/src/bb/bb_performance_catalogue.inc'
keep = {l.strip() for l in (here / 'firmware-keys.txt').read_text().splitlines() if l.strip() and not l.startswith('#')}
blob = bbpc.read_inc(inc); value = bbpc.decode(blob)
present = set()
def keys(x):
    if isinstance(x, dict):
        for k, y in x.items(): present.add(k); keys(y)
    elif isinstance(x, list):
        for y in x: keys(y)
keys(value)
named = set()
for f in glob.glob(str(tree / 'firmware/**/*'), recursive=True):
    if f.endswith(('.c', '.h', '.inc')) and not f.endswith('bb_performance_catalogue.inc'):
        named |= set(re.findall(r'"([A-Za-z_][A-Za-z_0-9]*)"', open(f, errors='replace').read()))
gap = sorted((named & present) - keep)
if gap:
    sys.exit(f'firmware names catalogue fields missing from firmware-keys.txt: {gap}')
if '--check' in sys.argv:
    print(f'firmware-keys.txt covers every catalogue field the firmware names ({len(named & present)} named, {len(keep)} kept)')
    sys.exit(0)
def strip(x):
    if isinstance(x, dict): return {k: strip(y) for k, y in x.items() if k in keep}
    if isinstance(x, list): return [strip(y) for y in x]
    return x
out = strip(value)
new, _ = bbpc.encode(out, version=3)
assert bbpc.decode(new) == out, 'round trip'
if new == blob:
    print('already stripped'); sys.exit(0)
inc.write_text(bbpc.inc_text(new, 'BBPC v3: independent raw-deflate fixed-Huffman 768-byte metadata pages; unchanged record IDs and text. '
                                  'Only the fields in catalogue-generator/firmware-keys.txt (the fields the firmware reads) are kept.'))
print(json.dumps({'before_bytes': len(blob), 'after_bytes': len(new), 'saving': len(blob) - len(new),
                  'after_sha256': hashlib.sha256(new).hexdigest()}))
