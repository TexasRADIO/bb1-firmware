#!/usr/bin/env python3
"""Build a driver on the real audio owner with the candidate's 539-section oracle compile (integration-480.c,
tests/oracle/compile.json, ASan/UBSan unless --fast). With --user-kits the kit builder is compiled in exactly as
the 2.0 recipe turns it on: every -D in build/recipes/composition-20-lean/recipe.json extra_flags that names
BB_WITH_USER_KITS, plus the bb_user_kit unit its link_order adds (one source of truth for host and native).
usage: build.py SRC OUT [--fast] [--user-kits] [--driver FILE [--mock]] [-DX=1 ...] [extra.c ...]"""
import json, subprocess, sys, os
src = os.path.abspath(sys.argv[1]); out = sys.argv[2]; args = sys.argv[3:]
here = os.path.dirname(os.path.abspath(__file__))
fast = '--fast' in args; driver = None; extra = []; i = 0
while i < len(args):
    a = args[i]
    if a == '--driver': driver = args[i + 1]; i += 2; continue
    if a == '--user-kits':
        recipe = json.load(open(src + '/big-beat/integration/build/recipes/composition-20-lean/recipe.json'))
        flags = [f for f in recipe['extra_flags'] if 'BB_WITH_USER_KITS' in f]
        assert flags and 'bb_user_kit' in recipe['link_order'], 'the 2.0 recipe does not turn the kit builder on'
        extra += flags + [src + '/firmware/src/bb/bb_user_kit.c']
    elif a.startswith(('-D', '-U', '-I')): extra.append(a)
    elif a.endswith('.c'): extra.append(a.replace('@SRC@', src))
    i += 1
ORA = src + '/big-beat/integration/tests/oracle'
cmd = json.load(open(ORA + '/compile.json'))
if driver and '--mock' in args:
    body = open(here + '/cpu1_mock.inc').read() + open(driver).read()
else:
    body = open(driver or ORA + '/integration-480.c').read()
dp = out + '.driver.c'; open(dp, 'w').write(body.replace('@SRC@', src))
c = []
for a in cmd:
    a = a.replace('@SRC@', src).replace('@OUT@', out).replace('@ORACLE@', dp)
    if a == '-Wl,-dead_strip': a = '-Wl,--gc-sections'
    if fast and a in ('-fsanitize=address,undefined', '-fno-sanitize-recover=all'): continue
    if fast and a == '-O1': a = '-O2'
    c.append(a)
c += extra + ['-lm']
r = subprocess.run(c, capture_output=True, text=True)
open(out + '.log', 'w').write(' '.join(c) + '\n' + r.stdout + r.stderr)
errs = [l for l in (r.stdout + r.stderr).splitlines() if 'error' in l]
print('\n'.join(errs[:40]) or 'ok'); sys.exit(r.returncode)
