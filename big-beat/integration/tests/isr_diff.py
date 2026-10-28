#!/usr/bin/env python3
"""Address-normalised function comparison of two pi32v2 objdump -d listings.
usage: isr_diff.py OLD.dis NEW.dis FUNCS.txt  -> prints changed / missing interrupt-reachable functions."""
import re, sys
def funcs(p):
    d, cur = {}, None
    for l in open(p, errors='replace'):
        m = re.match(r'^([A-Za-z_][\w.]*):$', l.strip())
        if m: cur = m.group(1); d[cur] = []; continue
        m = re.match(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*\t(.*)$', l)
        if cur and m:
            t = re.sub(r'<([\w.]+)(\+0x[0-9A-F]+)? : [0-9a-f]+ >', r'<\1\2>', m.group(1))
            t = re.sub(r'\b(call|goto|if\b.*goto)\s+-?\d+', r'\1 _', t)
            t = re.sub(r'\s*#?\s*</src/[^>]*>', '', t)               # file-relative constant annotations
            t = re.sub(r'\b(2[89]\d{6}|3\d{7})\b', 'ADDR', t)         # absolute RAM/XIP addresses (0x1Bxxxxx-0x2xxxxxx)
            t = re.sub(r'= -?\d{6,} <', '= _ <', t)
            t = re.sub(r'<([\w.]+)\+0x[0-9A-F]+>', r'<\1+off>', t)    # branch offsets inside moved callers
            d[cur].append(t.strip())
    return d
a, b = funcs(sys.argv[1]), funcs(sys.argv[2])
names = [n.strip() for n in open(sys.argv[3]) if n.strip()]
changed = [n for n in names if n in a and n in b and a[n] != b[n]]
missing = [n for n in names if n not in b]
print(f'interrupt-reachable: {len(names)}; changed {len(changed)}: {changed}; missing in new: {missing}')
new = [n for n in b if n not in a]; print('functions only in new:', new[:20])
