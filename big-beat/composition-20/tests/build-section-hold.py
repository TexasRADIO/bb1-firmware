#!/usr/bin/env python3
"""Build the P3a HOLD-mask proofs (section_hold_test.c) on this tree's real section owner, as build-section-owner.py
does (the oracle's own compile.json, ASan/UBSan), plus the arranger source units for the SONG producer check.
-dead_strip becomes --gc-sections off macOS. usage: build-section-hold.py OUT [-DP3A_DIFF_ONLY=1]
Run: OUT/test-section-hold          (8.1.1-8.1.8, prints PASS n checks)
     OUT/test-section-hold diff     (keep=0 SONG-only digest; equal on the parent tree = SONG byte-identical)"""
from pathlib import Path
import sys,json,subprocess
src=Path(__file__).resolve().parents[3]
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
fixture=src/'big-beat/integration/tests/oracle'
oracle=out/'oracle.c';oracle.write_text((fixture/'integration-480.c').read_text().replace('@SRC@',str(src)))
args=[x.replace('@SRC@',str(src)).replace('@OUT@',str(out/'test-section-hold')).replace('@ORACLE@',str(src/'big-beat/composition-20/tests/section_hold_test.c')) for x in json.loads((fixture/'compile.json').read_text())]
if sys.platform!='darwin':args=['-Wl,--gc-sections' if a=='-Wl,-dead_strip' else a for a in args]
args+=['-DORACLE_C="'+str(oracle)+'"','-Werror=implicit-function-declaration',
       '-DP3A_BUNDLE_SIZE=1432','-DP3A_HANDOFF_SIZE=504',   # 8425ff5 host layout: keep sits in existing padding
       str(src/'firmware/src/bb/bb_arrangement.c'),str(src/'firmware/src/bb/bb_arr_sources.c')]+sys.argv[2:]
(out/'compile.json').write_text(json.dumps(args,indent=2)+'\n')
p=subprocess.run(args,capture_output=True,text=True);(out/'BUILD.log').write_text(p.stdout+p.stderr)
if p.returncode:print(p.stdout+p.stderr[-4000:])
sys.exit(p.returncode)
