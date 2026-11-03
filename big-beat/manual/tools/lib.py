"""Lane M manual builder: drawing helpers (keycaps, gestures, FM-1 panel, chips) and the source trace.
Everything is drawn here as inline SVG/HTML; nothing is copied from any other guide's artwork."""
import html, os, re, subprocess

# ------------------------------------------------------------------ trace
TREES = {}          # name -> path (set by build)
TRACE = []          # (where, sentence, source)
_WHERE = ['']
_LINECACHE = {}
MISSES = []
_TREE_IDS = {}

def where(w): _WHERE[0] = w

def resolve(src):
    """src: 'tree:path::pattern' (tree d4 or sr), 'site:path::pattern', or free text (tool output). -> 'file:line'"""
    if '::' not in src:
        return src
    loc, pat = src.split('::', 1)
    tree, path = loc.split(':', 1) if ':' in loc else ('d4', loc)
    root = TREES[tree]
    full = os.path.join(root, path)
    key = (full, pat)
    if key not in _LINECACHE:
        if not os.path.isfile(full):
            MISSES.append(src); return 'MISSING ' + src
        with open(full, encoding='utf-8', errors='replace') as f:
            lines = f.read().split('\n')
        n = next((i + 1 for i, l in enumerate(lines) if pat in l), None)
        if n is None:
            MISSES.append(src); return 'MISSING ' + src
        prefix = tree_identity(tree)
        _LINECACHE[key] = f'{path}:{n} ({prefix})'
    return _LINECACHE[key]

def tree_identity(tree):
    root = os.path.abspath(TREES[tree])
    if root not in _TREE_IDS:
        try:
            head = subprocess.check_output(['git', '-C', root, 'rev-parse', 'HEAD'], text=True, stderr=subprocess.DEVNULL).strip()
            dirty = subprocess.check_output(['git', '-C', root, 'status', '--porcelain'], text=True).strip()
            _TREE_IDS[root] = head + (' (working tree modified)' if dirty else '')
        except subprocess.CalledProcessError:
            _TREE_IDS[root] = 'unversioned input: ' + root
    return _TREE_IDS[root]


def S(text, *srcs):
    """A factual sentence with its source(s); returns the text (with inline markup)."""
    TRACE.append((_WHERE[0], plain(text), '; '.join(resolve(s) for s in srcs) if srcs else 'framing (no factual claim)'))
    return text

def plain(t):
    return re.sub(r'\s+', ' ', re.sub(r'\{([^}|]+)(\|[^}]*)?\}', r'[\1]', re.sub(r'<[^>]+>', '', t))).strip()

# ------------------------------------------------------------------ inline chips
BUTTONS = {'FX', 'SCL', 'ENV', 'LFO', 'EDIT', 'GLO', 'HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC', 'OCT−', 'OCT+', 'OCT−/+'}
ENCODERS = {'SELECT', 'ALGO', 'PRESETS', 'MASTER'}

def chip(name):
    n = name
    if n in BUTTONS: cls = 'btn'
    elif n in ENCODERS: cls = 'enc'
    elif re.fullmatch(r'K[1-4](-K?[1-4])?', n): cls = 'knob'
    elif re.fullmatch(r'B\d+(-\d+)?', n): cls = 'blk'
    elif re.fullmatch(r'\d+(-\d+)?', n): cls = 'wht'
    else: cls = 'btn'
    return f'<kbd class="kc kc-{cls}">{html.escape(n)}</kbd>'

def md(t):
    """inline markup: {HOME} chip, {+} plus, {>} then, **b**, `screen text`"""
    def rep(m):
        x = m.group(1)
        if x == '+': return '<span class="op">+</span>'
        if x == '>': return '<span class="op">→</span>'
        return chip(x)
    t = re.sub(r'\{([^}]+)\}', rep, t)
    t = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', t)
    t = re.sub(r'`(.+?)`', r'<span class="scr">\1</span>', t)
    return t

# ------------------------------------------------------------------ text fitting (Wanted widths measured in Chromium: tools/README)
def est_w(label, fs):
    w = 0.0
    for ch in label:
        w += 1.07 if ch in 'MW' else 0.6 if ch.isdigit() else 0.45 if ch in ' .·' else 0.86
    return w * fs
def fit(label, avail, base):
    """largest font size <= base whose estimated width fits avail (the browser check in tools/fitcheck.py proves it)"""
    return round(min(base, base * avail / max(1.0, est_w(label, base))), 1)

# ------------------------------------------------------------------ keycaps (isometric, drawn)
def _cap(x, y, w, h, label, kind, sub=None):
    face = {'btn': 'var(--cap)', 'wht': 'var(--capw)', 'blk': 'var(--ink)', 'acc': 'var(--accent)'}.get(kind, 'var(--cap)')
    ink = 'onb' if kind == 'blk' else ''
    d = 6
    s = (f'<path d="M{x+3},{y+h} L{x+3+d},{y+h+d} L{x+w+d},{y+h+d} L{x+w+d},{y+3+d} L{x+w},{y+3}" fill="var(--ink)"/>'
         f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="{face}" stroke="var(--ink)" stroke-width="2.5"/>')
    fs = fit(label, w - 10, 15)
    s = s.replace(f'<rect x="{x}" y="{y}"', f'<g class="fit"><rect x="{x}" y="{y}"', 1)
    s += f'<text x="{x+w/2}" y="{y+h/2+fs*0.36}" text-anchor="middle" class="capt {ink}" font-size="{fs}">{html.escape(label)}</text></g>'
    if sub:
        s += f'<text x="{x+w/2}" y="{y+h+d+14}" text-anchor="middle" class="capsub" font-size="10" fill="var(--mute)">{html.escape(sub)}</text>'
    return s

def _knob(x, y, r, label, big=False):
    cx, cy = x + r, y + r
    s = (f'<circle cx="{cx+4}" cy="{cy+4}" r="{r}" fill="var(--ink)"/>'
         f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="var(--capw)" stroke="var(--ink)" stroke-width="2.5"/>'
         f'<rect x="{cx-2}" y="{cy-r+5}" width="4" height="{r*0.55:.0f}" rx="2" fill="var(--ink)"/>')
    s += f'<text x="{cx}" y="{cy+r+20}" text-anchor="middle" class="capt" font-size="{12 if big else 13}" fill="var(--ink)">{html.escape(label)}</text>'
    return s

def _flag(cx, y, word):
    w = 10 + 8.2 * len(word)
    return (f'<rect x="{cx-w/2}" y="{y}" width="{w}" height="17" fill="var(--accent)"/>'
            f'<text x="{cx}" y="{y+12.5}" text-anchor="middle" class="flag" font-size="11" fill="var(--ink)">{html.escape(word)}</text>'
            f'<path d="M{cx-5},{y+20} L{cx+5},{y+20} L{cx},{y+26} Z" fill="var(--accent)"/>')

def gesture(spec, scale=1.0):
    """spec tokens: 'HOLD:REC', 'TAP:REC', 'TURN:K4', 'REC', 'B1', '7', 'K1', '+', '>', '1!' (count text).
    Returns an inline SVG of keycaps joined by + and →."""
    toks = spec.split()
    x, parts, H = 8, [], 120
    for t in toks:
        if t in ('+', '>'):
            parts.append(f'<text x="{x+12}" y="{70}" text-anchor="middle" class="op2" font-size="28" fill="var(--ink)">{"+" if t=="+" else "→"}</text>')
            x += 26; continue
        flag = None
        if ':' in t:
            flag, t = t.split(':', 1)
            flag = {'HOLD': 'HOLD!', 'TAP': 'TAP!', 'TURN': 'TURN!', 'PRESS': 'PRESS!'}.get(flag, flag)
        lab = t.replace('_', ' ')
        if lab in ENCODERS or re.fullmatch(r'K[1-4]', lab):
            r = 26 if lab in ENCODERS else 22
            parts.append(_knob(x, 38, r, lab, lab in ENCODERS))
            if flag: parts.append(_flag(x + r, 6, flag))
            x += 2 * r + 22
        else:
            kind = 'blk' if re.fullmatch(r'B\d+(-\d+)?', lab) else 'wht' if re.fullmatch(r'\d+(-\d+)?', lab) else 'btn'
            w = max(58, 12 + 9.5 * len(lab))
            parts.append(_cap(x, 40, w, 52, lab, kind))
            if flag: parts.append(_flag(x + w / 2, 8, flag))
            x += w + 20
    W = x + 4
    return (f'<svg class="gest" viewBox="0 0 {W:.0f} {H}" width="{W*scale:.0f}" height="{H*scale:.0f}" role="img" '
            f'aria-label="{html.escape(spec.replace(":", " ").replace(">", "then"))}">{"".join(parts)}</svg>')

# ------------------------------------------------------------------ the FM-1 front panel (flat, drawn from the measured control boxes)
ENC = {'MASTER': [124, 117, 219, 237], 'SELECT': [299, 117, 394, 240], 'PRESETS': [114, 287, 217, 410], 'ALGO': [286, 286, 394, 413]}
KNOB = {'K1': [919, 113, 1019, 238], 'K2': [1108, 115, 1203, 238], 'K3': [1294, 112, 1389, 238], 'K4': [1477, 113, 1577, 236]}
BTN = {'FX': [962, 319, 1037, 394], 'SCL': [1066, 318, 1141, 393], 'ENV': [1170, 318, 1245, 393], 'LFO': [1274, 318, 1349, 393],
       'EDIT': [1378, 317, 1452, 392], 'GLO': [1482, 317, 1557, 392], 'HOME': [963, 421, 1038, 497], 'SAVE': [1068, 420, 1143, 496],
       'ARP': [1172, 420, 1247, 496], 'SEQ': [1276, 420, 1351, 495], 'PLAY': [1380, 419, 1455, 495], 'REC': [1483, 419, 1559, 495],
       'OCT−': [154, 461, 241, 509], 'OCT+': [284, 461, 371, 508]}
KEYS = [91, 585, 1652, 951]
SCREEN = [470, 70, 830, 430]
BPOS = [0.5, 1.5, 2.5, 4.5, 5.5, 7.5, 8.5, 9.5, 11.5, 12.5, 14.5]

def _led(cx, cy, led, onb):
    """the key's light as the firmware sets it (bb_nav_leds): bright = filled, half-lit = half-filled, pulse = dashed ring"""
    ink = 'var(--paper2)' if onb else 'var(--ink)'
    if led == 'B':
        return f'<circle cx="{cx:.1f}" cy="{cy}" r="10" fill="var(--accent)" stroke="{ink}" stroke-width="2.5"/>'
    if led == 'h':
        return (f'<circle cx="{cx:.1f}" cy="{cy}" r="10" fill="none" stroke="{ink}" stroke-width="2.5"/>'
                f'<path d="M{cx-10:.1f},{cy} A10,10 0 0 0 {cx+10:.1f},{cy} Z" fill="var(--accent)"/>')
    if led == 'p':
        return f'<circle cx="{cx:.1f}" cy="{cy}" r="10" fill="none" stroke="var(--accent)" stroke-width="4" stroke-dasharray="5 4"/>'
    return ''

def led_key():
    """the legend for the key lights drawn on the key maps"""
    o = ['<svg class="gest" viewBox="0 0 640 40" width="640" height="40" role="img" aria-label="Key lights: bright, half-lit, pulse, off">']
    for i, (led, word) in enumerate((('B', 'BRIGHT'), ('h', 'HALF-LIT'), ('p', 'PULSE'), ('.', 'OFF'))):
        x = 14 + i * 160
        o.append(_led(x, 20, led, False) if led != '.' else f'<circle cx="{x}" cy="20" r="10" fill="none" stroke="var(--line)" stroke-width="2"/>')
        o.append(f'<text x="{x+18}" y="26" class="capt" font-size="16">{word}</text>')
    o.append('</svg>')
    return ''.join(o)

def panel(lit=(), screen=None, keys=None, labels=True, alt=''):
    """lit: control names to highlight. keys: dict 'W1'..'W16' / 'B1'..'B11' -> label, or (label, LED letter)
    (highlighted keys, label printed on key, the key's light from bb_nav_leds drawn as a mark)."""
    lit = set(lit); keys = {k: (v if isinstance(v, tuple) else (v, '')) for k, v in (keys or {}).items()}
    o = ['<svg class="panel" viewBox="0 0 1750 1020" role="img" aria-label="%s">' % html.escape(alt or 'FM-1 front panel')]
    o.append('<rect x="0" y="0" width="1750" height="1020" fill="var(--dev)" stroke="var(--ink)" stroke-width="4"/>')
    o.append(f'<rect x="{SCREEN[0]-30}" y="{SCREEN[1]-24}" width="{SCREEN[2]-SCREEN[0]+60}" height="{SCREEN[3]-SCREEN[1]+48}" fill="#000" stroke="var(--ink)" stroke-width="4"/>')
    if screen:
        o.append(f'<image href="{screen}" x="{SCREEN[0]}" y="{SCREEN[1]}" width="{SCREEN[2]-SCREEN[0]}" height="{SCREEN[3]-SCREEN[1]}"/>')
    def hl(n): return n in lit
    for n, b in list(ENC.items()) + list(KNOB.items()):
        cx, cy, r = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2 + 8, 46 if n in ENC else 42
        on = hl(n)
        o.append(f'<circle cx="{cx+6}" cy="{cy+6}" r="{r}" fill="var(--ink)" opacity="{1 if on else .25}"/>')
        o.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{"var(--capw)" if on else "var(--devc)"}" stroke="var(--ink)" stroke-width="{5 if on else 2}"/>')
        o.append(f'<rect x="{cx-4}" y="{cy-r+8}" width="8" height="24" fill="var(--ink)" opacity="{1 if on else .4}"/>')
        if labels:
            o.append(f'<text x="{cx}" y="{b[1]-10}" text-anchor="middle" class="plab{" on" if on else ""}" font-size="{fit(n, 150, 30)}">{n}</text>')
    for n, b in BTN.items():
        on = hl(n); w, h = b[2] - b[0], b[3] - b[1]
        o.append(f'<rect x="{b[0]+6}" y="{b[1]+6}" width="{w}" height="{h}" fill="var(--ink)" opacity="{1 if on else .25}"/>')
        fs = fit(n, w - 12, 26)
        o.append(f'<g class="fit"><rect x="{b[0]}" y="{b[1]}" width="{w}" height="{h}" fill="{"var(--capw)" if on else "var(--devc)"}" stroke="var(--ink)" stroke-width="{5 if on else 2}"/>')
        o.append(f'<text x="{b[0]+w/2}" y="{b[1]+h/2+fs*0.36}" text-anchor="middle" class="plab{" on" if on else ""}" font-size="{fs}">{n}</text></g>')
    wx, ww = KEYS[0] + 14, (KEYS[2] - KEYS[0] - 28) / 16
    for i in range(16):
        k = f'W{i+1}'; on = k in keys or 'KEYS' in lit or 'WHITE' in lit
        x, y, w, h = wx + i * ww + 6, KEYS[1] + 188, ww - 12, 150
        o.append(f'<rect x="{x+6:.1f}" y="{y+6}" width="{w:.1f}" height="{h}" fill="var(--ink)" opacity="{1 if on else .2}"/>')
        o.append(f'<g class="fit"><rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" fill="{"var(--capw)" if on else "var(--devk)"}" stroke="var(--ink)" stroke-width="{4 if on else 2}"/>')
        o.append(f'<text x="{x+w/2:.1f}" y="{y+h-16}" text-anchor="middle" class="knum{" on" if on else ""}" font-size="{fit(str(i+1), w - 10, 30)}">{i+1}</text>')
        if k in keys and keys[k][0]:
            o.append(f'<text x="{x+w/2:.1f}" y="{y+44}" text-anchor="middle" class="krole" font-size="{fit(keys[k][0], w - 10, 24)}">{html.escape(keys[k][0])}</text>')
        if k in keys: o.append(_led(x + w / 2, y + 82, keys[k][1], False))
        o.append('</g>')
    for i, p in enumerate(BPOS):
        k = f'B{i+1}'; on = k in keys or 'KEYS' in lit or 'BLACK' in lit
        x, y, w, h = wx + (p + 0.5) * ww - ww * 0.34, KEYS[1] + 26, ww * 0.68, 136
        o.append(f'<rect x="{x+6:.1f}" y="{y+6}" width="{w:.1f}" height="{h}" fill="var(--ink)" opacity="{1 if on else .2}"/>')
        o.append(f'<g class="fit"><rect x="{x:.1f}" y="{y}" width="{w:.1f}" height="{h}" fill="{"var(--ink)" if on else "var(--devb)"}" stroke="var(--ink)" stroke-width="{4 if on else 2}"/>')
        o.append(f'<text x="{x+w/2:.1f}" y="{y+h-14}" text-anchor="middle" class="knum{" onb" if on else ""}" font-size="{fit("B%d" % (i+1), w - 8, 26)}">B{i+1}</text>')
        if k in keys and keys[k][0]:
            o.append(f'<text x="{x+w/2:.1f}" y="{y+34}" text-anchor="middle" class="krole onb" font-size="{fit(keys[k][0], w - 8, 20)}">{html.escape(keys[k][0])}</text>')
        if k in keys: o.append(_led(x + w / 2, y + 72, keys[k][1], True))
        o.append('</g>')
    o.append('</svg>')
    return ''.join(o)
