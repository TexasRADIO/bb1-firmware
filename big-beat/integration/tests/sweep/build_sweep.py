#!/usr/bin/env python3
"""Compile sweep_levels.c against a firmware source tree, reusing the integration fixture's recorded
compile command (claude-code/timing/tests/integration/command.json): same -D set and unit list,
re-rooted onto SOURCE. usage: build_sweep.py SOURCE OUT [--asan]"""
import json, os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
src, out = os.path.abspath(sys.argv[1]), sys.argv[2]
asan = '--asan' in sys.argv
rec = json.load(open(os.path.join(here, '../../../timing/tests/integration/command.json')))
old = '/Volumes/Orange Base/Codex Working Cache/tmp/sample-audio-r758/source-release-cymbal-key-r1'
cmd, i = [], 0
while i < len(rec):
    a = rec[i]
    if a == '-o': cmd += ['-o', out]; i += 2; continue
    if a.endswith('/test.c'): cmd.append(os.path.join(here, 'sweep_levels.c'))
    elif old in a: cmd.append(a.replace(old, src))
    elif a == '-I/Volumes/Orange Base/Codex Working Cache/tmp': pass
    elif a in ('-fsanitize=address,undefined', '-fno-sanitize-recover=all') and not asan: pass
    elif a == '-O1': cmd.append('-O1' if asan else '-O2')
    elif a == '-Wl,--gc-sections': cmd.append('-Wl,-dead_strip')
    else: cmd.append(a)
    i += 1
cmd += ['-I' + os.path.join(src, 'firmware/hal'), '-lm']
for extra in ['bb_phrase.c', 'bb_phrase_index.c', 'bb_phrase_controls.c', 'bb_delayed.c', 'bb_audio_phrase.c',
              'bb_audio_phrase_render.c', 'bb_phrase_shaker.c', 'bb_phrase_strike.c', 'bb_catalogue.c',
              'bb_paged_records.c', 'bb_paged_container.c', 'bb_catalogue_page.c']:
    p = os.path.join(src, 'firmware/src/bb', extra)
    if os.path.exists(p) and p not in cmd: cmd.append(p)
r = subprocess.run(cmd, capture_output=True, text=True)
open(out + '.log', 'w').write(' '.join(cmd) + '\n' + r.stdout + r.stderr)
print('\n'.join(l for l in (r.stdout + r.stderr).splitlines() if 'error' in l or 'Undefined' in l or '"_' in l)[:3000] or 'ok')
sys.exit(r.returncode)
