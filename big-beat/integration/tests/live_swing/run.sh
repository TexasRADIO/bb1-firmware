#!/bin/bash
# Lane Q pre-launch checks on a 2.0 source tree (test code only; no firmware file is changed).
#   usage: run.sh TREE OUT
#   1. live_swing   N02 matrix, SONG queued sections, count-in/swung recording, tap tempo on the real audio owner
#   2. tap_nav      REC+GLO tap on the real bb_nav.c
#   3. project_file the tree's own composition-20 project-file transaction test (F01: SONG/project never marked
#                   saved on a failed or unverified write); Linux only needs -dead_strip -> --gc-sections and a
#                   writable temporary directory instead of macOS /private/tmp (both changed in OUT, not in TREE)
set -uo pipefail
SRC=$(realpath "$1"); OUT=$(realpath -m "$2"); mkdir -p "$OUT"; export ASAN_OPTIONS=detect_leaks=0
T=$SRC/big-beat/integration/tests/live_swing
python3 -I "$T/build.py" "$SRC" "$T/live_swing_test.c" "$OUT/live_swing" > "$OUT/live_swing.build.log" 2>&1 || echo "live_swing: COMPILE FAIL"
"$OUT/live_swing" > "$OUT/live_swing.txt" 2>&1; echo "live_swing rc=$?: $(tail -1 "$OUT/live_swing.txt")"
"$OUT/live_swing" repro > "$OUT/live_swing.repro.txt" 2>&1
clang -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -I "$SRC/firmware/src" "$T/tap_nav_test.c" \
  "$SRC/firmware/src/bb_nav.c" -o "$OUT/tap_nav" > "$OUT/tap_nav.build.log" 2>&1 || echo "tap_nav: COMPILE FAIL"
"$OUT/tap_nav" > "$OUT/tap_nav.txt" 2>&1; echo "tap_nav rc=$?: $(tail -1 "$OUT/tap_nav.txt")"
python3 -I "$SRC/big-beat/composition-20/tests/build-project-file.py" "$OUT/pf" > /dev/null 2>&1
mkdir -p "$OUT/pftmp"; sed -i "s#/private/tmp/bb1-project-file-XXXXXX#$OUT/pftmp/bb1-project-file-XXXXXX#" "$OUT/pf/project-arranger.c"
python3 -I - "$OUT/pf/compile.json" <<'PY'
import json, subprocess, sys
a = ['-Wl,--gc-sections' if x == '-Wl,-dead_strip' else x for x in json.load(open(sys.argv[1]))]
sys.exit(subprocess.run(a, capture_output=True).returncode)
PY
(cd "$OUT/pf" && ./test-arranger > ../project_file.txt 2>&1); echo "project_file rc=$?: $(tail -1 "$OUT/project_file.txt")"
