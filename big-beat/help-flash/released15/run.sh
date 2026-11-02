#!/bin/bash
# usage (from the source root): big-beat/help-flash/released15/run.sh OLD.bin NEW.bin
# The 12 reader files here are the released 1.5 ones (bb1-firmware v1.5 e3d41c5; Git blobs in PINS.json, checked below).
set -euo pipefail
R=big-beat/help-flash/released15; O=$(mktemp -d)
python3 -I - "$R" <<'PY'
import json, subprocess, sys
r = sys.argv[1]
for f in json.load(open(f'{r}/PINS.json'))['files']:
    h = subprocess.run(['git', 'hash-object', f'{r}/{f["file"]}'], capture_output=True, text=True, check=True).stdout.strip()
    assert h == f['git_blob'], f'{f["file"]}: {h} != released {f["git_blob"]}'
print('released 1.5 reader: 12 files match their pinned Git blobs')
PY
# the released files first on the include path, so the 1.5 reader (not the 2.0 copies) is what runs
clang -std=c11 -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -fno-omit-frame-pointer -I "$R" -I firmware/src \
  "$R/test_released15_reader.c" "$R/bb_crc32.c" "$R/bb_wire.c" -o "$O/t"
ASAN_OPTIONS=detect_leaks=0 "$O/t" "$1" "$2"
