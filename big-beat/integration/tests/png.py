#!/usr/bin/env python3
"""Turn capture.c's raw RGB565 frames into PNGs (2x, as the UI goldens) and a contact sheet.
Standard library only. usage: png.py CAPTURES.raw OUTDIR"""
import os, struct, sys, zlib

def png(path, w, h, rows):
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\0' + r for r in rows)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def rgb(v): return bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))

data, outdir = open(sys.argv[1], 'rb').read(), sys.argv[2]
os.makedirs(outdir, exist_ok=True)
frames, at = [], 0
while at < len(data):
    end = data.index(b'\0', at); name = data[at:end].decode(); at = end + 1
    px = struct.unpack('<57600H', data[at:at + 115200]); at += 115200
    frames.append((name, px))
for name, px in frames:
    rows = []
    for y in range(240):
        r = b''.join(rgb(px[y * 240 + x]) * 2 for x in range(240))
        rows += [r, r]
    png(os.path.join(outdir, name + '.png'), 480, 480, rows)
cols = 8; n = len(frames); rws = (n + cols - 1) // cols; W, H = 240 * cols + 4 * (cols - 1), 240 * rws + 4 * (rws - 1)
sheet = [bytearray(b'\x40' * (W * 3)) for _ in range(H)]
for i, (name, px) in enumerate(frames):
    ox, oy = (i % cols) * 244, (i // cols) * 244
    for y in range(240):
        sheet[oy + y][ox * 3:(ox + 240) * 3] = b''.join(rgb(px[y * 240 + x]) for x in range(240))
png(os.path.join(outdir, 'contact-sheet.png'), W, H, [bytes(r) for r in sheet])
open(os.path.join(outdir, 'index.txt'), 'w').write('\n'.join(f'{i:3} {nm}' for i, (nm, _) in enumerate(frames)) + '\n')
print(f'{n} PNGs + contact-sheet.png ({cols} per row) in {outdir}')
