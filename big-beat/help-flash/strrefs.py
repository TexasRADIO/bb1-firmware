#!/usr/bin/env python3
"""String-literal -> referencing section map for one native relocatable object (readelf only).
usage: strrefs.py OBJ OUT.json"""
import json, re, subprocess, sys
obj, out = sys.argv[1], sys.argv[2]
sym = subprocess.run(['readelf', '-sW', obj], capture_output=True, text=True).stdout
secs = subprocess.run(['readelf', '-SW', obj], capture_output=True, text=True).stdout
strsec = None
for l in secs.splitlines():
    m = re.match(r'\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+([0-9a-f]+)\s+([0-9a-f]+)', l)
    if m and m.group(2) == '.rodata.str1.1':
        strsec, off, size = int(m.group(1)), int(m.group(3), 16), int(m.group(4), 16)
data = open(obj, 'rb').read()[off:off + size]
strs = {}
for l in sym.splitlines():
    p = l.split()
    if len(p) >= 8 and p[6] == str(strsec):
        a = int(p[1], 16); n = int(p[2])
        strs[p[7]] = (a, n)
rel = subprocess.run(['readelf', '-rW', obj], capture_output=True, text=True).stdout
refs = {}
cur = None
for l in rel.splitlines():
    m = re.match(r"Relocation section '\.rela(\S+)'", l)
    if m: cur = m.group(1); continue
    m = re.match(r'\s*[0-9a-f]{8}\s+[0-9a-f]{8}\s+.*?\s[0-9a-f]{8}\s+(\S+)', l)
    if cur and m and m.group(1) in strs:
        refs.setdefault(m.group(1), set()).add(cur)
res = []
for k, (a, n) in strs.items():
    s = data[a:a + n].rstrip(b'\0').decode('latin1')
    res.append({'sym': k, 'off': a, 'bytes': n, 'text': s, 'refs': sorted(refs.get(k, []))})
res.sort(key=lambda r: r['off'])
json.dump(res, open(out, 'w'), indent=0)
print(len(res), 'strings', sum(r['bytes'] for r in res), 'B; unreferenced', sum(1 for r in res if not r['refs']))
