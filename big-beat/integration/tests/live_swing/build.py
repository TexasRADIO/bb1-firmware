#!/usr/bin/env python3
"""Build a Lane Q pre-launch driver on the real 2.0 audio owner, exactly as pattern_edit/build.py does (the oracle
driver's CPU1 stand-in, integration-480.c up to its 'integrated bb_audio comparison' marker, + DRIVER, compiled with
the oracle's own compile.json; ASan/UBSan on; -dead_strip becomes --gc-sections off macOS), plus the arranger units.
Adds -DBB_RICH_TRACE=1 so every rich strike reaches the driver's bb_rich_trace(). No firmware file is modified.
usage: build.py SRC DRIVER OUT [-DX=1 ...]"""
import json, subprocess, sys
from pathlib import Path
src, driver, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
defs = [a for a in sys.argv[4:] if a.startswith('-D')]
T = src / 'big-beat/integration/tests/oracle'
prefix = (T / 'integration-480.c').read_text().split('/* ---- integrated bb_audio comparison')[0]
body = prefix.replace('@SRC@', str(src)) + '\n' + driver.read_text()
d = out.with_suffix('.driver.c'); d.write_text(body)
cmd = []
for a in json.loads((T / 'compile.json').read_text()):
    a = a.replace('@SRC@', str(src)).replace('@OUT@', str(out)).replace('@ORACLE@', str(d))
    cmd.append('-Wl,--gc-sections' if a == '-Wl,-dead_strip' and sys.platform != 'darwin' else a)
cmd += [str(src / f'firmware/src/bb/{u}.c') for u in ('bb_gen', 'bb_live_record', 'bb_arrangement', 'bb_arr_sources',
                                                     'bb_arr_controller')]
cmd += ['-DBB_WITH_LIVE_CAPTURE=1', '-UBB_RICH_TRACE', '-DBB_RICH_TRACE=1', '-Wno-macro-redefined', *defs, '-lm']
r = subprocess.run(cmd, capture_output=True, text=True)
sys.stderr.write(r.stderr[-6000:])
sys.exit(r.returncode)
