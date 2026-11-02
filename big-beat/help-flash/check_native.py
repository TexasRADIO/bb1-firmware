#!/usr/bin/env python3
"""Native gate for the help-flash build (readelf only, no device).
usage: check_native.py BASE_OUT NEW_OUT     (two nbuild.sh output directories)

  1. RAM: .data, .bss, .ram_text and .noinit sizes equal to the base; bb_tx_state is in .pool.
  2. Reach: no path from an interrupt handler, CPU1, RAM text, recovery, console, OTA or the start-up code that runs
     before persist_boot to bb_tx / bb_tx_ready (section-level reference graph over every object's relocations;
     static, so it may over-include but cannot miss a direct reference).
  3. Fit: report.json deltas (code, RAM, pool)."""
import json, re, subprocess, sys
from pathlib import Path

def sh(*a): return subprocess.run(a, capture_output=True, text=True, check=True).stdout
def secsizes(elf):
    out = {}
    for l in sh('readelf', '-SW', str(elf)).splitlines():
        m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)', l)
        if m: out[m.group(1)] = int(m.group(2), 16)
    return out

def graph(outdir):
    defs, edges = {}, {}
    for o in sorted(Path(outdir).glob('*.o')):
        secname = {}
        for l in sh('readelf', '-SW', str(o)).splitlines():
            m = re.match(r'\s*\[\s*(\d+)\]\s+(\S+)', l)
            if m: secname[m.group(1)] = m.group(2)
        for l in sh('readelf', '-sW', str(o)).splitlines():
            p = l.split()
            if len(p) >= 8 and p[6] in secname and p[3] in ('FUNC', 'OBJECT', 'NOTYPE', 'SECTION'):
                name = p[7] if p[3] != 'SECTION' else secname[p[6]]
                key = name if p[4] == 'GLOBAL' else (o.name, name)
                defs[key] = (o.name, secname[p[6]])
                if p[4] != 'GLOBAL': defs.setdefault(name, (o.name, secname[p[6]]))
        cur = None
        for l in sh('readelf', '-rW', str(o)).splitlines():
            m = re.match(r"Relocation section '\.rela(\S+)'", l)
            if m: cur = (o.name, m.group(1)); edges.setdefault(cur, set()); continue
            m = re.match(r'\s*[0-9a-f]{8}\s+[0-9a-f]{8}\s+.*?\s[0-9a-f]{8}\s+(\S+)', l)
            if cur and m: edges[cur].add((o.name, m.group(1)))
    def resolve(ref):
        o, s = ref
        return defs.get((o, s)) or defs.get(s)
    return defs, {k: {resolve(r) for r in v} - {None} for k, v in edges.items()}

# Hard roots: code that may run with XIP off or on CPU1 (interrupts, RAM text, CPU1), and code that runs before
# persist_boot maps the plain window (USB safe-mode recovery, persist_boot itself, crash capture).
ROOT = re.compile(r'_irq$|_isr$|^\.ram_text|^\.c1_text|bb_c1_worker|recovery|persist_boot|bb_crash|first_fault|uboot')
# Reported only: main-loop code with XIP on (console commands, the app-update session's idle MIDI service)
INFO = re.compile(r'^con_|console|^ota_')
def main():
    base, new = Path(sys.argv[1]), Path(sys.argv[2])
    res, ok = {}, True
    a, b = secsizes(base / 'candidate/app.elf'), secsizes(new / 'candidate/app.elf')
    res['ram_sections'] = {k: [a.get(k), b.get(k)] for k in ('.data', '.bss', '.ram_text', '.noinit', '.pool')}
    ok &= all(a.get(k) == b.get(k) for k in ('.data', '.bss', '.ram_text', '.noinit'))
    sym = sh('readelf', '-sW', str(new / 'candidate/app.elf'))
    st = re.search(r'([0-9a-f]{8})\s+\d+\s+OBJECT\s+LOCAL\s+DEFAULT\s+(\d+)\s+bb_tx_state', sym)
    pool = [l for l in sh('readelf', '-SW', str(new / 'candidate/app.elf')).splitlines() if ' .pool ' in l]
    res['bb_tx_state'] = st.group(1) if st else None
    m = re.search(r'\.pool\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)', pool[0]) if pool else None
    lo, hi = (int(m.group(1), 16), int(m.group(1), 16) + int(m.group(2), 16)) if m else (0, 0)
    ok &= bool(st) and lo <= int(st.group(1), 16) < hi
    defs, edges = graph(new)
    target = {v for k, v in defs.items() if (k if isinstance(k, str) else k[1]) in ('bb_tx', 'bb_tx_ready')}
    roots = [k for k in edges if ROOT.search(k[1].replace('.text.', '', 1) if k[1].startswith('.text.') else k[1])]
    seen, stack, parent, hits = set(roots), list(roots), {}, []
    while stack:
        n = stack.pop()
        if n in target: hits.append(n)
        for m in edges.get(n, ()):
            if m not in seen: seen.add(m); parent[m] = n; stack.append(m)
    def path(n):
        p = [n]
        while p[-1] in parent: p.append(parent[p[-1]])
        return ' <- '.join(x[1] for x in p)
    res['reach_roots'] = len(roots); res['reach_sections'] = len(seen)
    info = [k for k in edges if INFO.search(k[1][6:] if k[1].startswith('.text.') else k[1])]
    iseen, istack, ihits = set(info), list(info), 0
    while istack:
        n = istack.pop(); ihits += n in target
        for m in edges.get(n, ()):
            if m not in iseen: iseen.add(m); istack.append(m)
    res['info_main_loop_console_or_update_reach_bb_tx'] = bool(ihits)
    res['bb_tx_reached_from_roots'] = [path(h) for h in hits]
    ok &= not hits
    ra, rb = (json.loads((d / 'report.json').read_text())['candidate'] for d in (base, new))
    res['fit'] = {k: [ra[k], rb[k], rb[k] - ra[k]] for k in ('bytes', 'code_free', 'ram_free', 'pool_free', 'ram_text_bytes')}
    res['app_sha256'] = rb['sha256']; res['passed'] = bool(ok)
    print(json.dumps(res, indent=1))
    sys.exit(0 if ok else 1)

if __name__ == '__main__': main()
