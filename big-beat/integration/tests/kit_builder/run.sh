#!/bin/bash
# 2.0 kit builder host suite. Host only; nothing touches a device.
#   usage: run.sh BASE_TREE TREE WORK_DIR [sections]
# BASE_TREE  the candidate the branch starts from (2501f70), unpatched
# TREE       this branch (the kit builder on in the 2.0 recipe)
# Gates: 1 oracle (539 sections): base / TREE flag off / TREE flag on (recipe flags), digests and all section digests equal
#        2 168 BB1 1.1 scenes play and re-encode identically in all three builds
#        3 user kits (ASan/UBSan, flag on): codec, resolution, routes vs d097e15, pads, one-machine identity
#          (prototype install, the firmware's BUILD swap, the firmware's kit+phrase USER load), live swaps,
#          paired scenes v4 and BBP2 project round trip through the firmware's own scene codec
#        4 BUILD navigation on the real bb_nav_input (flag off and on) and the BUILD route matrix
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
BASE=$(realpath "$1"); TREE=$(realpath "$2"); W=$(realpath -m "$3"); SECTIONS=${4:-}
LIB=${LIB:-/home/claude/big-beat-fm1-review/handover/assets/sound-library-region.bin}   # pinned f4bbf1a4: LinnDrum DrumTraks DMX + AUX
mkdir -p "$W"; cd "$W"
B="python3 $HERE/build.py"
echo "== 1 oracle"
$B "$BASE" o-base --fast; $B "$TREE" o-flag0 --fast; $B "$TREE" o-flag1 --fast --user-kits
for x in o-base o-flag0 o-flag1; do ./$x > $x.out 2> $x.sections; echo "$x $(head -c 64 $x.out) $(sed -n 2p $x.out | grep -o 'sizeof_audio=[0-9]*')"; done
# line 1 = the 539-section digest; line 2 also carries sizeof(bb_audio), which grows by the two USER pointers
cmp <(head -1 o-base.out) <(head -1 o-flag0.out) && cmp <(head -1 o-base.out) <(head -1 o-flag1.out) && \
  cmp o-base.sections o-flag0.sections && cmp o-base.sections o-flag1.sections && \
  echo "oracle: digest and all $(wc -l < o-base.sections) section digests identical (flag off; flag on, no USER kit)"
echo "== 2 scenes"
SC="--driver $HERE/driver-scenes.c @SRC@/firmware/src/bb/bb_audio_session.c"
$B "$BASE" s-base --fast $SC; $B "$TREE" s-flag0 --fast $SC; $B "$TREE" s-flag1 --fast $SC --user-kits
./s-base make s11.bin >/dev/null
for x in s-base s-flag0 s-flag1; do ./$x play s11.bin 2> $x.txt >/dev/null; done
cmp s-base.txt s-flag0.txt && cmp s-base.txt s-flag1.txt && echo "scenes: $(wc -l < s-base.txt) BB1 1.1 scenes play identically (file $(sha256sum s11.bin | cut -c1-16))"
echo "== 3 user kits (ASan/UBSan)"
$B "$TREE" uk-test --user-kits --driver "$HERE/test_user_kit.c" --mock -DBB_SAMPLE_OWNER_SLOTS=24 \
  @SRC@/firmware/src/bb/bb_arrangement.c @SRC@/firmware/src/bb/bb_arr_sources.c @SRC@/firmware/src/bb/bb_project.c
./uk-test "$LIB" $SECTIONS > uk.out 2> uk.err || { grep -v '^ROUTE' uk.out; tail -20 uk.err; exit 1; }
grep -v '^ROUTE' uk.out
python3 "$HERE/check_routes.py" uk.out "$HERE/routes-d097e15.csv"
echo "== 4 BUILD navigation (bb_nav_input, ASan/UBSan)"
for f in 0 1; do
  clang -std=c11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I "$TREE/firmware/src" \
    -DBB_WITH_USER_KITS=$f "$TREE/big-beat/integration/tests/nav_build.c" "$TREE/firmware/src/bb_nav.c" -o nav_build$f
  ./nav_build$f build-route-matrix$f.csv
done
