#!/bin/sh
# Lane LP-P4: render the manual's screens, key map, catalogue and section-page probe from a firmware tree ($1), into $2
set -e
T=$1; O=$2; H=$(cd "$(dirname "$0")" && pwd); mkdir -p "$O"
L=$(grep -n '^int main' "$T/big-beat/integration/tests/capture.c" | cut -d: -f1)
head -n $((L-1)) "$T/big-beat/integration/tests/capture.c" > "$O/cap_prelude.h"
F=$(python3 "$H/profile_flags.py" "$T")
cd "$T"
U=$(python3 -c "import re;t=open('big-beat/integration/verify_release.py').read();m=re.search(r\"CAPTURE_UNITS = \\[f'firmware/src/bb/\\{u\\}.c' for u in \\(([^)]*)\\)\",t,re.S);print(' '.join('firmware/src/bb/'+x.strip().strip(\"'\")+'.c' for x in m.group(1).split(',') if x.strip()))")
clang -std=c11 -O1 -w $F -Dsection\(x\)=used -I "$O" -I "$H" -I firmware/src -I build/gen -I firmware/src/bb -I firmware/src/bb/x0x -I firmware/hal "$H/manual_capture.c" firmware/src/bb_nav.c $U -lm -o "$O/manual_capture"
"$O/manual_capture" "$O/manual.raw" > "$O/legends.txt"; tail -1 "$O/legends.txt"
clang -w $F -I firmware/src -I build/gen "$H/keymap.c" firmware/src/bb_nav.c -o "$O/keymap" && "$O/keymap" > "$O/keymap.json"
clang -w $F -I firmware/src -I build/gen "$H/secpage_probe.c" firmware/src/bb_nav.c -o "$O/secpage_probe" && "$O/secpage_probe" > "$O/secpage_probe.txt"
. big-beat/integration/tests/host_flags.sh
clang -std=c11 -O1 -w -Dsection\(x\)=used $HOST_DEFS $HOST_INC "$H/catcount.c" $(for f in bb_factory bb_factory_recipe bb_browser bb_catalogue bb_paged_container bb_paged_records bb_catalogue_page bb_crc32 bb_core; do echo firmware/src/bb/$f.c; done) -lm -o "$O/catcount" && "$O/catcount" > "$O/catalogue.txt"
python3 "$H/raw2png.py" "$O/manual.raw" "$O/png" 2
