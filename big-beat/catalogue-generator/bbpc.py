#!/usr/bin/env python3
"""BBPC v2 codec for the Big Beat performance catalogue (r9 step 4).

Decoder authority: firmware/src/bb/bb_paged_container.c, bb_paged_records.c and
bb_catalogue_page.c (boot39). This module mirrors those rules and adds an encoder:

  * Records are interned bottom-up (children before parents) by a post-order walk,
    deduplicated by value, so every child/shape reference is to a lower ID.
  * An object interns its values first, then its key array (the "shape").
  * Text records own consecutive slices of the text region in ID order.
  * Shapes are listed in the dictionary sorted by their array record ID.
  * Metadata is cut into 768-byte pages. Each page is LZ4-packed with the
    project's shortest-parse packer (a line-for-line port of compress() in
    claude/library-audit-14/tools/repack_catalogue.mjs); a page stays raw when
    the packed form is not strictly smaller.

Gate: encode(decode(installed)) == installed, byte for byte (see pack.py).
No device access.
"""
import json
import math
import struct
import zlib

PAGE = 768
MAGIC = b'BBPC'


# ---------------------------------------------------------------- LZ4 page codec
def lz4_decode(src, size):
    out = bytearray()
    r = 0

    def ext(n):
        nonlocal r
        if n == 15:
            while True:
                b = src[r]
                r += 1
                n += b
                if b != 255:
                    break
        return n

    while r < len(src):
        tok = src[r]
        r += 1
        lit = ext(tok >> 4)
        out += src[r:r + lit]
        r += lit
        if r == len(src):
            break
        off = src[r] | src[r + 1] << 8
        r += 2
        assert 0 < off <= len(out)
        for _ in range(ext(tok & 15) + 4):
            out.append(out[-off])
    if len(out) != size:
        raise ValueError('LZ4 page decodes to %d bytes, expected %d' % (len(out), size))
    return bytes(out)


def _extra(n):
    return 0 if n < 15 else (n - 15) // 255 + 1


def _lengths(out, n):
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)


def lz4_compress(src):
    """Shortest-parse LZ4 block, port of repack_catalogue.mjs compress().

    Same match search (all prior positions with the same 4-byte key, newest first,
    strictly-longer wins), same end-of-block limits (matches start before n-12 and
    stop before n-5) and the same cost model and tie-breaking, so the output is
    identical to the JavaScript packer's.
    """
    n = len(src)
    assert 0 < n <= PAGE
    longest = [0] * n
    distance = [0] * n
    chains = {}
    for i in range(n - 12):
        key = src[i:i + 4]
        prior = chains.setdefault(key, [])
        for j in range(len(prior) - 1, -1, -1):
            p = prior[j]
            length = 4
            while i + length < n - 5 and src[p + length] == src[i + length]:
                length += 1
            if length > longest[i]:
                longest[i] = length
                distance[i] = i - p
        prior.append(i)
    inf = float('inf')
    costs = [0.0] * (n + 1)
    matches = [inf] * n
    take = [0] * n
    nxt = [-1] * n
    for anchor in range(n - 1, -1, -1):
        for length in range(4, longest[anchor] + 1):
            c = _extra(length - 4) + costs[anchor + length]
            if c < matches[anchor]:
                matches[anchor] = c
                take[anchor] = length
        remaining = n - anchor
        costs[anchor] = 1 + remaining + _extra(remaining)
        for at in range(anchor, n - 12):
            literal = at - anchor
            c = 3 + literal + _extra(literal) + matches[at]
            if c < costs[anchor]:
                costs[anchor] = c
                nxt[anchor] = at
    out = bytearray()
    anchor = 0
    while nxt[anchor] >= 0:
        at = nxt[anchor]
        lit = at - anchor
        length = take[at]
        off = distance[at]
        out.append((min(lit, 15) << 4) | min(length - 4, 15))
        if lit >= 15:
            _lengths(out, lit - 15)
        out += src[anchor:at]
        out += bytes((off & 255, off >> 8))
        if length >= 19:
            _lengths(out, length - 19)
        anchor = at + length
    lit = n - anchor
    out.append(min(lit, 15) << 4)
    if lit >= 15:
        _lengths(out, lit - 15)
    out += src[anchor:n]
    assert len(out) == costs[0]
    out = bytes(out)
    assert lz4_decode(out, n) == bytes(src)
    return out


# ---------------------------------------------------------------- container
def _var(value):
    if not 0 <= value <= 65535:
        raise ValueError('record reference out of range: %d' % value)
    out = bytearray()
    while value >= 128:
        out.append((value & 127) | 128)
        value >>= 7
    out.append(value)
    return bytes(out)


def _framed(first, body):
    n = 2 + len(body)
    if n < 255:
        return bytes((first, n)) + body
    n += 4
    if n < 259:
        raise ValueError('unencodable record length %d' % n)
    return bytes((first, 255)) + struct.pack('<I', n) + body


def crc(blob):
    return zlib.crc32(bytes(blob[:12]) + bytes(blob[16:])) & 0xffffffff


class Container:
    """Parsed BBPC: header fields, shapes, raw metadata, text, page directory."""

    def __init__(self, blob):
        blob = bytes(blob)
        u16 = lambda o: struct.unpack_from('<H', blob, o)[0]
        u32 = lambda o: struct.unpack_from('<I', blob, o)[0]
        if blob[:4] != MAGIC or u16(4) != 2 or u16(6) != PAGE or u32(8) != len(blob):
            raise ValueError('not a BBPC v2 container')
        if u32(12) != crc(blob):
            raise ValueError('CRC mismatch')
        self.blob = blob
        self.nodes, self.root, self.metadata_bytes = u32(16), u32(20), u32(24)
        self.nshapes = u16(28)
        self.sparse, self.dictionary, self.blocks = u32(32), u32(36), u32(40)
        self.block_count, self.text_at, self.text_bytes, self.payload = u32(44), u32(48), u32(52), u32(56)
        self.shapes = [(u16(self.dictionary + 4 * i), u16(self.dictionary + 4 * i + 2)) for i in range(self.nshapes)]
        self.text = blob[self.text_at:self.text_at + self.text_bytes]
        self.pages = []  # (offset, raw_flag, encoded_len, decoded_len)
        meta = bytearray()
        for i in range(self.block_count):
            off, field, size = struct.unpack_from('<IHH', blob, self.blocks + 8 * i)
            enc = blob[self.payload + off:self.payload + off + (field & 32767)]
            self.pages.append((off, field >> 15, field & 32767, size))
            meta += enc if field & 32768 else lz4_decode(enc, size)
        if len(meta) != self.metadata_bytes:
            raise ValueError('metadata length mismatch')
        self.meta = bytes(meta)
        self._index()

    def _vars(self, pos, count):
        out = []
        for _ in range(count):
            v = shift = 0
            while True:
                b = self.meta[pos]
                pos += 1
                v |= (b & 127) << shift
                shift += 7
                if b < 128:
                    break
            out.append(v)
        return out, pos

    def _index(self):
        """records[id] = (kind, payload): number bits, text bytes, child ids, (shape token, child ids)."""
        self.records = []
        self.offsets = []
        at = 0
        m = self.meta
        for _ in range(self.nodes):
            self.offsets.append(at)
            tag = m[at]
            kind = tag & 7
            if kind <= 2:
                self.records.append((kind, None))
                at += 1
            elif kind == 3:
                self.records.append((kind, m[at + 1:at + 9]))
                at += 9
            elif kind == 4:
                n, off = struct.unpack_from('<HI', m, at + 1)
                self.records.append((kind, self.text[off:off + n]))
                at += 7
            else:
                n, header = m[at + 1], 2
                if n == 255:
                    n, header = struct.unpack_from('<I', m, at + 2)[0], 6
                pos, end = at + header, at + n
                if kind == 5:
                    (count,), pos = self._vars(pos, 1)
                    kids, pos = self._vars(pos, count)
                    self.records.append((5, tuple(kids)))
                else:
                    token = tag >> 3
                    if token == 31:
                        (extra,), pos = self._vars(pos, 1)
                        token += extra
                    kids, pos = self._vars(pos, self.shapes[token][1])
                    self.records.append((6, (self.shapes[token][0], tuple(kids))))
                assert pos == end
                at = end
        assert at == self.metadata_bytes

    def value(self, ident=None):
        """Decode to Python (object_pairs preserved; numbers as float unless integral)."""
        memo = {}

        def go(i):
            if i in memo:
                return memo[i]
            kind, p = self.records[i]
            if kind == 0:
                v = None
            elif kind == 1:
                v = False
            elif kind == 2:
                v = True
            elif kind == 3:
                x = struct.unpack('<d', p)[0]
                v = int(x) if math.isfinite(x) and x == int(x) and abs(x) < 2 ** 53 else x
            elif kind == 4:
                v = p.decode('utf-8')
            elif kind == 5:
                v = [go(c) for c in p]
            else:
                keys = go(p[0])
                v = dict(zip(keys, [go(c) for c in p[1]]))
            memo[i] = v
            return v

        # Return fresh copies: memoised shared objects must not alias on edit.
        return json.loads(json.dumps(go(self.root if ident is None else ident)))


def decode(blob):
    return Container(blob).value()


# ---------------------------------------------------------------- encoder
def _number_bits(x):
    if isinstance(x, bool):
        raise TypeError('bool is not a number')
    return struct.pack('<d', float(x))


def encode(value, raw_pages=False, appends=()):
    """Encode a JSON value as BBPC v2. Returns (bytes, report).

    value      the base layer, interned canonically: an object's values first,
               then its key array (pack_bbpc.py / boot39 / boot42 order).
    appends    later layers, each appended append-only exactly as
               claude/library-audit-14/tools/append_catalogue.py does: every
               earlier record keeps its ID and bytes, an object's key array is
               interned before its values, and the last layer becomes the root.
               Records of earlier roots that the new root no longer reaches stay
               in the container (unreachable, never read).
    """
    records = []   # (kind, payload) in ID order
    seen = {}

    def intern(rec):
        got = seen.get(rec)
        if got is not None:
            return got
        ident = len(records)
        if ident >= 65535:
            raise ValueError('more than 65535 records')
        records.append(rec)
        seen[rec] = ident
        return ident

    def walk(x, shape_first):
        if x is None:
            return intern((0, None))
        if x is False:
            return intern((1, None))
        if x is True:
            return intern((2, None))
        if isinstance(x, (int, float)):
            if isinstance(x, float) and not math.isfinite(x):
                raise ValueError('non-finite number')
            return intern((3, _number_bits(x)))
        if isinstance(x, str):
            b = x.encode('utf-8')
            if len(b) > 65535:
                raise ValueError('text too long')
            return intern((4, b))
        if isinstance(x, list):
            return intern((5, tuple(walk(v, shape_first) for v in x)))
        if isinstance(x, dict):
            if shape_first:
                shape = walk(list(x.keys()), shape_first)
                kids = tuple(walk(v, shape_first) for v in x.values())
            else:
                kids = tuple(walk(v, shape_first) for v in x.values())
                shape = walk(list(x.keys()), shape_first)
            return intern((6, (shape, kids)))
        raise TypeError(type(x))

    root = walk(value, False)
    for layer in appends:
        root = walk(layer, True)
    shape_ids = sorted({p[0] for k, p in records if k == 6})
    token_of = {sid: t for t, sid in enumerate(shape_ids)}
    meta, text, sparse = bytearray(), bytearray(), []
    for ident, (kind, p) in enumerate(records):
        if ident % 16 == 0:
            sparse.append(len(meta))
        if kind <= 2:
            meta.append(kind)
        elif kind == 3:
            meta += b'\x03' + p
        elif kind == 4:
            meta += b'\x04' + struct.pack('<HI', len(p), len(text))
            text += p
        elif kind == 5:
            meta += _framed(5, _var(len(p)) + b''.join(_var(c) for c in p))
        else:
            t = token_of[p[0]]
            body = (_var(t - 31) if t >= 31 else b'') + b''.join(_var(c) for c in p[1])
            meta += _framed(6 | (min(t, 31) << 3), body)
    if len(shape_ids) > 65535:
        raise ValueError('too many shapes')
    directory, payload = bytearray(), bytearray()
    for at in range(0, len(meta), PAGE):
        page = bytes(meta[at:at + PAGE])
        packed = None if raw_pages else lz4_compress(page)
        if packed is not None and len(packed) < len(page):
            enc, field = packed, len(packed)
        else:
            enc, field = page, 0x8000 | len(page)
        directory += struct.pack('<IHH', len(payload), field, len(page))
        payload += enc
    sparse_at = 64
    dict_at = sparse_at + 4 * len(sparse)
    blocks_at = dict_at + 4 * len(shape_ids)
    text_at = blocks_at + len(directory)
    payload_at = text_at + len(text)
    out = bytearray(payload_at + len(payload))
    struct.pack_into('<4sHHIIIIIHHIIIIIII', out, 0, MAGIC, 2, PAGE, len(out), 0,
                     len(records), root, len(meta), len(shape_ids), 0,
                     sparse_at, dict_at, blocks_at, len(directory) // 8, text_at, len(text), payload_at)
    for i, o in enumerate(sparse):
        struct.pack_into('<I', out, sparse_at + 4 * i, o)
    for t, sid in enumerate(shape_ids):
        struct.pack_into('<HH', out, dict_at + 4 * t, sid, len(records[sid][1]))
    out[blocks_at:text_at] = directory
    out[text_at:payload_at] = text
    out[payload_at:] = payload
    struct.pack_into('<I', out, 12, crc(out))
    report = dict(bytes=len(out), records=len(records), root=root, shapes=len(shape_ids),
                  metadata=len(meta), text=len(text), pages=len(directory) // 8,
                  raw_pages=sum(1 for i in range(0, len(directory), 8) if directory[i + 5] & 0x80))
    return bytes(out), report


# ---------------------------------------------------------------- C include
def read_inc_text(src):
    import re
    body = src.split('{', 1)[1].rsplit('}', 1)[0]
    return bytes(int(h, 16) for h in re.findall(r'0x([0-9a-fA-F]{2})', body))


def read_inc(path):
    return read_inc_text(open(path, encoding='utf-8').read())


def inc_text(blob, comment):
    lines = ['/* SPDX-License-Identifier: GPL-3.0-only */', '/* %s */' % comment,
             'static const unsigned char bb_performance_catalogue[] = {']
    for at in range(0, len(blob), 16):
        lines.append('    ' + ','.join('0x%02x' % b for b in blob[at:at + 16]) + ',')
    lines.append('};')
    return '\n'.join(lines) + '\n'
