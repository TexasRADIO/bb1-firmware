#!/usr/bin/env python3
"""raw2png.py CAPTURES.raw OUTDIR [scale]: every capture.c frame (name NUL + 240x240 RGB565 LE) to OUTDIR/<name>.png."""
import os, struct, sys, zlib
src, out = sys.argv[1], sys.argv[2]; sc = int(sys.argv[3]) if len(sys.argv) > 3 else 2
d = open(src, 'rb').read(); at = 0; os.makedirs(out, exist_ok=True)
def rgb(v): return bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
def ch(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
n = 0
while at < len(d):
    e = d.index(b'\0', at); name = d[at:e].decode(); at = e + 1; px = d[at:at + 115200]; at += 115200
    rows = []
    for y in range(240):
        r = b''.join(rgb(v) * sc for v in struct.unpack_from('<240H', px, y * 480))
        rows += [r] * sc
    open(os.path.join(out, name + '.png'), 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', 240 * sc, 240 * sc, 8, 2, 0, 0, 0)) +
        ch(b'IDAT', zlib.compress(b''.join(b'\0' + r for r in rows), 9)) + ch(b'IEND', b''))
    n += 1
print(n, 'frames')
