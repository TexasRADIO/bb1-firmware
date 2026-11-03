#!/usr/bin/env python3
"""BB1 2.0 help and long UI text in flash (Lane H). File-only: no device, USB, VM or flash transport.

  help_flash.py convert TREE [--min N] [--native STRS.json]
                                         move the selected literals of TREE's UI sources into bb_text_block.def,
                                         rewrite them as BB_TX(TX_...), then run gen. Re-runnable on a new
                                         candidate (for example after D4): new texts that meet the rule move too.
  help_flash.py gen TREE [--check]       bb_text_block.def -> bb_text_pin.h, bb_text_block_image.inc and
                                         build/help-flash/bb_text_block.bin. --check fails if any committed
                                         file differs from what gen would write.
  help_flash.py compose LIB BLOCK OUT    sound-library image + block at region A + 0x37000 -> OUT (a new file).
                                         Refuses unless LIB keeps 0x37000..0x39000 erased and BBSK ends before it;
                                         reads its output back; prints the byte-diff map and the hashes.
  help_flash.py replace LIB BLOCK OUT    replace one valid existing BBTX block in that guarded window.
                                         Bytes outside the window and loader-record scan must remain identical.

Selection rule (convert), for each string literal in the UI files of UI_FILES:
  * inside a function body, not in an excluded function (console, recovery, fault, startup, boot, OTA, USB, ISR,
    CPU1, RAM text); not in a static or char-array initialiser; not concatenated, indexed, offset or sizeof'd;
    not an argument of a comparison (cmp, eq, _is, match, find); not in asm or a preprocessor line;
  * at least --min bytes with its NUL (default 12) and at most 63 (the fallback is 64 bytes);
  * no '%' and none of the safety words in KEEP_WORDS (fault, save result, storage, library, USB, ...).
  * with --native (strrefs.py on the base build's main.o): only texts the native app really contains, so texts in
    code the build leaves out do not take flash.
Plus the help tables named in ID_TABLES, whose literals become bare IDs (any length).
"""
import argparse, hashlib, json, re, struct, sys, zlib
from pathlib import Path

DEF = 'firmware/src/bb/bb_text_block.def'
PIN = 'firmware/src/bb/bb_text_pin.h'
IMG = 'firmware/src/bb/bb_text_block_image.inc'
BIN = 'build/help-flash/bb_text_block.bin'
MAGIC, VERSION, HEADER = 0x58544242, 1, 32
FLASH_OFF, FLASH_MAX = 0xD7000, 0x2000
A_BASE, A_SIZE = 0xA0000, 245760
UI_FILES = """bb_record_firmware.inc bb_arranger_firmware.inc bb_arranger_draw.inc bb_nav.c bb_target_session.c
bb_dillo_kit_draw.inc bb_nav_firmware.inc bb_ui_v3/bb_ui.c bb_target_ui.c bb_pattern_edit_firmware.inc
bb_midi_ui_input.inc bb_midi_ui_draw.inc bb_gen_firmware.inc bb_gen_draw.inc bb_sound_picker_input.inc
bb_sound_picker_draw.inc bb_sample_sends_input.inc bb_sample_sends_draw.inc bb_ui_v3_firmware.inc
bb_dillo_kit_firmware.inc bb_project_firmware.inc bb_aux_browse.inc bb_home_sound_browse.inc bb_home_input.inc
bb_home_draw.inc home_master/input.inc home_master/screen.inc bb_playbook_face.inc bb_nav_draw.inc bb_kit_shelf.inc
bb_kit_first_entry.inc bb_design_help.inc group_knobs.inc group_mutes.inc bb_knob_contract.inc bb_mixer_contract.inc
aux_navigation_ui.inc home_navigation_ui.inc bb_ui_909.inc bb_rich_panel.inc bb_midi_settings_hold.inc
bb_user_kit_firmware.inc""".split()
# files that need the header (top-level translation units of the UI; the .inc files are included by them)
HEADER_HOSTS = {'bb_nav.c': '#include "bb/bb_text_block.h"', 'bb_ui_v3/bb_ui.c': '#include "bb/bb_text_block.h"',
                'bb_target_ui.c': '#include "bb/bb_text_block.h"', 'bb_target_session.c': '#include "bb/bb_text_block.h"'}
# help tables: file -> (table regex); their literals become bare IDs (the row type holds uint16_t IDs)
ID_TABLES = {'bb_ui_v3/bb_ui.c': r'static\s+const\s+u_help_row\s+HELP_\w+\s*\[\s*\]\s*=',
             'bb_nav.c': r'static\s+const\s+uint16_t\s+nav_messages\s*\[[^\]]*\]\s*='}
EXCLUDE_FN = re.compile(r'con_|console|recover|fault|crash|panic|startup|boot|ota|uboot|usb|uac|isr|irq|_c1_|^c1_|'
                        r'ram_|fm1_main|asset|transfer|probe|diag|flr|memr|power|splash|brand|lcd|init')
KEEP_WORDS = re.compile(r'FAULT|FAIL|ERROR|CRASH|RECOVER|PROTECTED|STORAGE|FLASH|SAVED|LIBRARY|MISSING|INVALID|PANIC|'
                        r'USB|UPDATE|RESTART|REBOOT|BATTERY|POWER|WATCHDOG', re.I)
KEEP_TEXTS = {b'Let go of HOME to return'}   # the help page's exit line: shown with the fallback too
CMP_CALL = re.compile(r'cmp|_eq$|^eq|str_eq|_is$|^is_|match|find|strstr|same')
TYPE_START = {'char', 'const', 'static', 'volatile', 'unsigned', 'signed', 'uint8_t', 'int8_t'}

# ------------------------------------------------------------------------------------------------ C literals
ESC = {'n': 10, 't': 9, 'r': 13, '0': 0, '\\': 92, '"': 34, "'": 39, 'a': 7, 'b': 8, 'f': 12, 'v': 11, '?': 63}
def c_bytes(lit):
    """Bytes of one C string literal token (with quotes), as the C compiler reads it (source is Latin-1/UTF-8 bytes)."""
    s, out, i = lit[1:-1], bytearray(), 0
    raw = s.encode('latin1')
    while i < len(raw):
        c = raw[i]
        if c != 92:
            out.append(c); i += 1; continue
        i += 1; e = chr(raw[i])
        if e == 'x':
            j = i + 1
            while j < len(raw) and chr(raw[j]) in '0123456789abcdefABCDEF': j += 1
            out.append(int(raw[i + 1:j], 16) & 0xFF); i = j
        elif e in '01234567':
            j = i
            while j < len(raw) and j < i + 3 and chr(raw[j]) in '01234567': j += 1
            out.append(int(raw[i:j], 8) & 0xFF); i = j
        else:
            out.append(ESC[e]); i += 1
    return bytes(out)

TOK = re.compile(r'''(?P<ws>[ \t\r\n]+)|(?P<com>/\*.*?\*/|//[^\n]*)|(?P<str>"(?:\\.|[^"\\\n])*")|(?P<chr>'(?:\\.|[^'\\\n])*')|'''
                 r'''(?P<id>[A-Za-z_]\w*)|(?P<num>\.?\d[\w.]*)|(?P<p>->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^!~<>=?:;,.(){}\[\]#])''', re.S)
def tokens(text):
    """Significant tokens: (kind, value, start, end). Preprocessor lines become one 'pp' token."""
    out, i, n, bol = [], 0, len(text), True
    while i < n:
        if bol:
            m = re.match(r'[ \t]*#', text[i:])
            if m:
                j = i
                while True:
                    k = text.find('\n', j)
                    if k < 0: k = n; break
                    if text[k - 1] == '\\': j = k + 1; continue
                    # a block comment that runs past the line end continues the directive
                    seg = text[i:k]
                    if seg.count('/*') > seg.count('*/'):
                        j = text.find('*/', k) + 2; continue
                    break
                out.append(('pp', text[i:k], i, k)); i = k; bol = True; continue
        m = TOK.match(text, i)
        if not m: raise SystemExit(f'tokenizer stuck at {text[i:i+40]!r}')
        kind = m.lastgroup; v = m.group()
        if kind == 'ws' and '\n' in v: bol = True
        elif kind not in ('ws', 'com'): bol = False
        if kind not in ('ws', 'com'): out.append((kind, v, m.start(), m.end()))
        i = m.end()
    return out

def scan(text, rel):
    """Candidate literals of one file: list of dicts with span, function, context verdict."""
    t = tokens(text); res = []
    depth, fn, fn_depth, stmt, fn_attr = 0, None, None, 0, False
    paren = []                                      # stack of (token index of '(')
    id_table = None; id_re = ID_TABLES.get(rel)
    ppstack = []                                    # #if branches: (depth at #if, depth at end of first branch)
    for i, (k, v, a, b) in enumerate(t):
        if k == 'pp':
            d = re.match(r'\s*#\s*(\w+)', v); d = d.group(1) if d else ''
            if d in ('if', 'ifdef', 'ifndef'): ppstack.append([depth, None, fn, fn_attr])
            elif d in ('elif', 'else') and ppstack:
                if ppstack[-1][1] is None: ppstack[-1][1] = depth
                depth, fn, fn_attr = ppstack[-1][0], ppstack[-1][2], ppstack[-1][3]   # each branch starts alike
            elif d == 'endif' and ppstack:
                e = ppstack.pop()
                if e[1] is not None: depth = e[1]   # assume the branches leave the same depth
            continue
        if k == 'p' and v == '{':
            if depth == 0:
                j = i - 1
                # skip __attribute__((...)) after the parameter list
                if t[j][1] == ')':
                    fn = None; d = 0; j2 = j
                    while j2 >= 0:
                        if t[j2][1] == ')': d += 1
                        elif t[j2][1] == '(':
                            d -= 1
                            if d == 0: break
                        j2 -= 1
                    name = t[j2 - 1][1] if j2 > 0 and t[j2 - 1][0] == 'id' else None
                    if name == '__attribute__' or name == '__attribute':
                        j3 = j2 - 1; d = 0
                        while j3 >= 0:
                            if t[j3][1] == ')': d += 1
                            elif t[j3][1] == '(':
                                d -= 1
                                if d == 0 and t[j3 - 1][0] == 'id' and t[j3 - 1][1] not in ('__attribute__',): name = t[j3 - 1][1]; break
                            j3 -= 1
                    head = ' '.join(x[1] for x in t[max(0, j2 - 12):j2])
                    if name and name not in ('if', 'for', 'while', 'switch', 'sizeof'):
                        fn, fn_depth = name, 1
                        fn_attr = 'section' in ' '.join(x[1] for x in t[j2:i]) or 'RAMFN' in head
                if id_re and depth == 0:
                    pre = text[max(0, a - 200):a]
                    if re.search(id_re + r'\s*$', pre): id_table = depth + 1
            depth += 1; stmt = i + 1; continue
        if k == 'p' and v == '}':
            depth -= 1
            if fn and depth == 0: fn = None
            if id_table and depth < id_table: id_table = None
            stmt = i + 1; continue
        if k == 'p' and v == ';': stmt = i + 1; continue
        if k == 'p' and v == '(': paren.append(i); continue
        if k == 'p' and v == ')':
            if paren: paren.pop()
            continue
        if k != 'str': continue
        why = []
        prev, nxt = t[i - 1] if i else ('', '', 0, 0), t[i + 1] if i + 1 < len(t) else ('', '', 0, 0)
        if id_table:
            res.append({'start': a, 'end': b, 'lit': v, 'fn': None, 'mode': 'id', 'why': []}); continue
        if not fn: why.append('file scope')
        elif EXCLUDE_FN.search(fn): why.append('excluded function ' + fn)
        elif fn_attr: why.append('RAM text function')
        if prev[0] == 'str' or nxt[0] == 'str': why.append('concatenated')
        if prev[0] == 'id' and prev[1] not in ('return', 'case'): why.append('macro or prefix before it')
        if nxt[1] in ('[', '+', '-'): why.append('indexed or offset')
        if prev[1] == 'sizeof' or (prev[1] == '(' and i > 1 and t[i - 2][1] == 'sizeof'): why.append('sizeof')
        st = t[stmt:i]
        sv = [x[1] for x in st]
        if 'static' in sv: why.append('static initialiser')
        if '__asm__' in sv or 'asm' in sv: why.append('asm')
        if sv and sv[0] in TYPE_START and '=' in sv:
            lhs = sv[:sv.index('=')]
            if '[' in lhs and '*' not in lhs: why.append('char array initialiser')
        if paren:
            o = paren[-1]
            call = t[o - 1][1] if o > 0 and t[o - 1][0] == 'id' else ''
            if CMP_CALL.search(call): why.append('comparison argument ' + call)
        res.append({'start': a, 'end': b, 'lit': v, 'fn': fn, 'mode': 'call', 'why': why})
    return res

def tx_name(data, used):
    w = re.sub(r'[^A-Z0-9]+', '_', data.decode('latin1').upper()).strip('_')[:40].rstrip('_') or 'X'
    name = 'TX_' + w
    if name in used and used[name] != data:
        n = 2
        while f'{name}_{n}' in used and used[f'{name}_{n}'] != data: n += 1
        name = f'{name}_{n}'
    used[name] = data
    return name

def c_escape(data):
    out = ''
    for ch in data:
        if ch in (34, 92): out += '\\' + chr(ch)
        elif 32 <= ch < 127 and ch != 63: out += chr(ch)
        else: out += '\\%03o' % ch
    return '"' + out + '"'

# ------------------------------------------------------------------------------------------------ convert
def convert(tree, mn, native=None):
    src = tree / 'firmware/src'
    picked, report, used, texts = {}, [], {}, {}
    defp = tree / DEF
    if defp.exists():                                   # keep the names of an earlier run (rebase-stable)
        for m in re.finditer(r'BB_TX_DEF\((\w+),\s*(".*")\)', defp.read_text(encoding='latin1')):
            used[m.group(1)] = c_bytes(m.group(2))
    # incremental re-run (refresh after D4 / swung recording): every entry of the existing .def that the firmware
    # still names stays in the block with its name; entries nothing names any more are pruned
    refd = set()
    for q in src.rglob('*'):
        if q.suffix in ('.c', '.h', '.inc') and q.name not in ('bb_text_block.def', 'bb_text_pin.h', 'bb_text_block_image.inc'):
            refd |= set(re.findall(r'\bTX_[A-Z0-9_]+', q.read_text(encoding='latin1')))
    seeded = {n for n in used if n in refd}
    pruned = sorted(n for n in used if n not in refd)
    for n in seeded: texts[n] = used[n]
    plan = {}
    for rel in UI_FILES:
        p = src / rel
        if not p.exists(): continue
        text = p.read_text(encoding='latin1')
        for c in scan(text, rel):
            data = c_bytes(c['lit'])
            if c['mode'] == 'call':
                n = len(data) + 1
                if n < mn: c['why'].append('short')
                if n > 64: c['why'].append('longer than the fallback')
                if b'%' in data: c['why'].append('format')
                if KEEP_WORDS.search(data.decode('latin1')): c['why'].append('safety text')
                if data in KEEP_TEXTS: c['why'].append('kept for the fallback page')
                if native is not None and data not in native: c['why'].append('not in the native build')
            if c['why']:
                if c['mode'] == 'call' and len(data) + 1 >= mn and 'not in the native build' not in c['why']:
                    report.append({'file': rel, 'text': data.decode('latin1'), 'kept': c['why']})
                continue
            if c['mode'] == 'id' and not data:
                plan.setdefault(rel, []).append((c, 'TX_NONE')); continue
            name = next((k for k, v in used.items() if v == data), None) or tx_name(data, used)
            texts[name] = data
            plan.setdefault(rel, []).append((c, name))
    idnames = {n for items in plan.values() for c, n in items if c['mode'] == 'id'}
    def size(names): return len(build_block([(n, texts[n]) for n in sorted(names)], check=False)[0])
    dropped = []
    while size(texts) > FLASH_MAX:
        n = min((n for n in texts if n not in idnames and n not in seeded), key=lambda n: (len(texts[n]), n))
        dropped.append(texts.pop(n).decode('latin1'))
    if dropped:
        for rel in plan: plan[rel] = [(c, n) for c, n in plan[rel] if n in texts or n == 'TX_NONE']
    for rel, items in plan.items():
        p = src / rel; text = p.read_text(encoding='latin1')
        for c, name in sorted(items, key=lambda x: -x[0]['start']):
            rep = name if c['mode'] == 'id' else f'BB_TX({name})'
            text = text[:c['start']] + rep + text[c['end']:]
        if rel in HEADER_HOSTS and HEADER_HOSTS[rel] not in text:
            # after the leading comment block / first include
            m = re.search(r'^#include[^\n]*\n', text, re.M)
            at = m.end() if m else 0
            text = text[:at] + HEADER_HOSTS[rel] + '\n' + text[at:]
        p.write_text(text, encoding='latin1')
    names = sorted(texts)
    lines = ['/* SPDX-License-Identifier: GPL-3.0-only */',
             '/* Generated by big-beat/help-flash/help_flash.py convert, then maintained here: the help and long UI texts',
             ' * that live in the BBTX flash block (bb_text_block.h). One entry per text; the order sets the IDs. After any',
             ' * change run: python3 big-beat/help-flash/help_flash.py gen . (and ship the new sound-library image). */']
    lines += [f'BB_TX_DEF({n}, {c_escape(texts[n])})' for n in names]
    defp.write_text('\n'.join(lines) + '\n', encoding='latin1')
    (tree / 'build/help-flash').mkdir(parents=True, exist_ok=True)
    (tree / 'build/help-flash/convert-report.json').write_text(json.dumps(
        {'min_bytes': mn, 'moved_texts': len(names), 'moved_bytes': sum(len(v) + 1 for v in texts.values()),
         'sites': sum(len(v) for v in plan.values()), 'files': {k: len(v) for k, v in plan.items()},
         'kept_from_def': len(seeded), 'pruned_unreferenced': pruned, 'new_texts': sorted(set(names) - seeded),
         'dropped_for_flash_budget': dropped,
         'kept_long_literals': report}, indent=1) + '\n')
    print(json.dumps({'texts': len(names), 'bytes': sum(len(v) + 1 for v in texts.values()),
                      'sites': sum(len(v) for v in plan.values()), 'kept_long': len(report)}))
    gen(tree, False)

# ------------------------------------------------------------------------------------------------ gen
def read_def(tree):
    out = []
    for m in re.finditer(r'^BB_TX_DEF\((\w+),\s*(".*")\)\s*$', (tree / DEF).read_text(encoding='latin1'), re.M):
        out.append((m.group(1), c_bytes(m.group(2))))
    return out

def crc(b): return zlib.crc32(b) & 0xFFFFFFFF

def build_block(entries, check=True):
    n = len(entries)
    body, placed = bytearray(), {}
    # longest first; a text that ends another one shares its bytes (tail merge, as the linker does for literals)
    for d in sorted({d for _, d in entries}, key=lambda d: (-len(d), d)):
        assert 0 not in d and len(d) + 1 <= 64, d
        hit = next((o + len(t) - len(d) for t, o in placed.items() if t.endswith(d)), None)
        if hit is None: hit = len(body); body += d + b'\0'
        placed[d] = hit
    offs = [2 * n + placed[d] for _, d in entries]
    payload = b''.join(struct.pack('<H', o) for o in offs) + bytes(body)
    assert len(payload) < 0x10000
    pc = crc(payload)
    hdr = struct.pack('<IBBHIIIII', MAGIC, VERSION, HEADER, n, len(payload), pc, 0, 0, 0)
    assert len(hdr) == HEADER - 4
    hdr += struct.pack('<I', crc(hdr))
    blk = hdr + payload
    assert not check or len(blk) <= FLASH_MAX, f'block {len(blk)} B is over {FLASH_MAX} B'
    return blk, pc

def gen(tree, check):
    entries = read_def(tree)
    blk, pc = build_block(entries)
    pin = ('/* Generated by big-beat/help-flash/help_flash.py gen from bb_text_block.def. Do not edit. */\n'
           f'#define BB_TX_PIN_COUNT {len(entries)}u\n#define BB_TX_PIN_BYTES {len(blk) - HEADER}u\n'
           f'#define BB_TX_PIN_CRC 0x{pc:08x}u\n#define BB_TX_BLOCK_SHA256 "{hashlib.sha256(blk).hexdigest()}"\n')
    rows = [', '.join('0x%02x' % x for x in blk[i:i + 16]) for i in range(0, len(blk), 16)]
    img = ('/* Generated by big-beat/help-flash/help_flash.py gen: the BBTX block, for host builds (tests, captures,\n'
           ' * browser emulator). The FM-1 reads the same bytes from flash 0xD7000. Do not edit. */\n'
           'static const uint8_t bb_tx_host_image[] __attribute__((unused)) = {\n' + ',\n'.join(rows) + '\n};\n')
    want = {PIN: pin, IMG: img}
    bad = [k for k, v in want.items() if not (tree / k).exists() or (tree / k).read_text() != v]
    if check:
        if bad: raise SystemExit('stale: ' + ', '.join(bad) + ' (run help_flash.py gen)')
    else:
        for k, v in want.items(): (tree / k).write_text(v)
        (tree / BIN).parent.mkdir(parents=True, exist_ok=True); (tree / BIN).write_bytes(blk)
    print(json.dumps({'texts': len(entries), 'block_bytes': len(blk), 'payload_crc': f'{pc:08x}',
                      'sha256': hashlib.sha256(blk).hexdigest(), 'checked' if check else 'written': True}))
    return blk

# ------------------------------------------------------------------------------------------------ compose
def loader_would_erase(img, base):
    """The update loader's record scan (firmware/loader/ldr_core.c, step 4) erases a sector whose last 256 B start
    with an 'AT' record with a valid CRC16. Returns the sectors of img that would match."""
    def crc16(b):
        c = 0
        for x in b:
            c ^= x << 8
            for _ in range(8): c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        return c
    hits = []
    for s in range(0x1000, len(img) + 1, 0x1000):
        r = img[s - 0x100:s - 0x100 + 80]
        rd = lambda o: r[o] | r[o + 1] << 8
        if rd(6) == 0x5441 and rd(0) and rd(0) == crc16(r[2:80]): hits.append(hex(base + s - 0x1000))
    return hits

def valid_block(B):
    if len(B) < HEADER or struct.unpack_from('<I', B, 0)[0] != MAGIC: raise SystemExit('refused: not a BBTX block')
    n, plen, pc = struct.unpack_from('<H', B, 6)[0], *struct.unpack_from('<II', B, 8)
    if len(B) != HEADER + plen or crc(B[HEADER:]) != pc or crc(B[:28]) != struct.unpack_from('<I', B, 28)[0]:
        raise SystemExit('refused: block header or CRC')
    return n

def compose(lib, block, out, replace=False):
    L, B = lib.read_bytes(), block.read_bytes()
    rel = FLASH_OFF - A_BASE
    if len(L) != A_SIZE: raise SystemExit(f'refused: library is {len(L)} B, not {A_SIZE}')
    if L[:4] != b'BBSK': raise SystemExit('refused: library does not start with BBSK')
    declared = struct.unpack_from('<I', L, 8)[0]
    if declared > rel: raise SystemExit(f'refused: BBSK declares {declared} B, past 0x{rel:x}')
    old_sha = None
    if replace:
        W = L[rel:rel + FLASH_MAX]
        if len(W) < HEADER or struct.unpack_from('<I', W, 0)[0] != MAGIC:
            raise SystemExit('refused: guarded window does not start with BBTX')
        old_len = HEADER + struct.unpack_from('<I', W, 8)[0]
        old = W[:old_len]
        valid_block(old)
        if any(x != 0xFF for x in W[old_len:]): raise SystemExit('refused: data follows the existing BBTX block')
        old_sha = hashlib.sha256(old).hexdigest()
        base = L[:rel] + b'\xFF' * FLASH_MAX + L[rel + FLASH_MAX:]
    else:
        if set(L[rel:rel + FLASH_MAX]) != {0xFF}: raise SystemExit('refused: 0x37000..0x39000 is not erased in the library')
        base = L
    if len(B) > FLASH_MAX: raise SystemExit('refused: BBTX block is larger than its guarded window')
    n = valid_block(B)
    img = base[:rel] + B + base[rel + len(B):]
    assert len(img) == A_SIZE
    diff = [i for i in range(A_SIZE) if img[i] != L[i]]
    if diff and (min(diff) < rel or max(diff) >= rel + FLASH_MAX): raise SystemExit('refused: diff outside the text area')
    hits = loader_would_erase(img, A_BASE)
    if hits != loader_would_erase(L, A_BASE): raise SystemExit(f'refused: loader record scan would erase {hits}')
    if out.exists() and out.resolve() in (lib.resolve(), block.resolve()): raise SystemExit('refused: would overwrite an input')
    out.parent.mkdir(parents=True, exist_ok=True); out.write_bytes(img)
    if out.read_bytes() != img: raise SystemExit('refused: readback differs')
    ivs, s = [], None
    for i, d in enumerate(diff):
        if s is None: s = d
        if i + 1 == len(diff) or diff[i + 1] != d + 1: ivs.append([s, d + 1]); s = None
    sha = lambda b: hashlib.sha256(b).hexdigest()
    print(json.dumps({'file': str(out), 'bytes': len(img), 'sha256': sha(img), 'base_sha256': sha(L),
                      'mode': 'replace' if replace else 'compose', 'old_block_sha256': old_sha,
                      'block_sha256': sha(B), 'block_bytes': len(B), 'texts': n, 'bbsk_declared': declared,
                      'changed_bytes': len(diff), 'changed_intervals_a_relative': ivs,
                      'text_area_a_relative': [rel, rel + FLASH_MAX], 'declared_bbsk_unchanged': img[:declared] == L[:declared],
                      'loader_scan_hits': hits, 'device_contact': False}, indent=1))

def main():
    ap = argparse.ArgumentParser(); sp = ap.add_subparsers(dest='cmd', required=True)
    c = sp.add_parser('convert'); c.add_argument('tree', type=Path); c.add_argument('--min', type=int, default=12)
    c.add_argument('--native', type=Path)
    g = sp.add_parser('gen'); g.add_argument('tree', type=Path); g.add_argument('--check', action='store_true')
    o = sp.add_parser('compose'); o.add_argument('lib', type=Path); o.add_argument('block', type=Path); o.add_argument('out', type=Path)
    r = sp.add_parser('replace'); r.add_argument('lib', type=Path); r.add_argument('block', type=Path); r.add_argument('out', type=Path)
    a = ap.parse_args()
    if a.cmd == 'convert':
        nat = {x['text'].encode('latin1') for x in json.loads(a.native.read_text())} if a.native else None
        convert(a.tree, a.min, nat)
    elif a.cmd == 'gen': gen(a.tree, a.check)
    else: compose(a.lib, a.block, a.out, a.cmd == 'replace')

if __name__ == '__main__': main()
