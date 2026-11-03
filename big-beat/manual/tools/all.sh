#!/bin/sh
# Lane LP-P4: the whole manual from a firmware tree: renders, guide, What's New, PDF (twice), screenshots, fitcheck
set -e
S=$(cd "$(dirname "$0")/.." && pwd); TIP=$1; APP=$2
cd "$S"; rm -rf out/png; tools/build.sh "$S/fw21" "$S/out" | grep -v '^legend'
git -C /home/claude/big-beat-fm1-review show origin/main:claude/2.1/rc2/perform-keymap.svg > out/png/perform-keymap.svg
sed -i "s/TIPSHORT/$TIP/; s/APPSHORT/$APP/" gen/build.py
B="python3 gen/build.py $S/fw21 $S/fw21 $S/site $S/out/png $S/out/catalogue.txt $S/out/keymap.json $S/out/TRACE.md"
$B | tail -40; python3 gen/whatsnew.py "$S/site"
export PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers
(cd site/public && python3 -m http.server 8765 --bind 127.0.0.1 >/dev/null 2>&1 & echo $! > "$S/out/http.pid"); sleep 1
python3 gen/pdf.py out/guide.pdf out/pages.txt "before you start" "hardware overview" "power on and install" "the screen" "keys and combos" "guide conventions" "workflow" "get started" "pages" "play and record" "functions" "how to" "system" "technical specifications" "sound library" "index"
MANUAL_PAGES=out/pages.txt $B | tail -1
python3 gen/pdf.py out/guide.pdf out/pages2.txt "before you start" "hardware overview" "power on and install" "the screen" "keys and combos" "guide conventions" "workflow" "get started" "pages" "play and record" "functions" "how to" "system" "technical specifications" "sound library" "index"
diff out/pages.txt out/pages2.txt && echo STABLE
mkdir -p out/shots; python3 gen/shots21.py out/shots
timeout 900 python3 gen/fitcheck.py > out/fitcheck.txt 2>&1 || true; tail -4 out/fitcheck.txt
kill $(cat out/http.pid) 2>/dev/null || true
