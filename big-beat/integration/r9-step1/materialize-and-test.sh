#!/bin/bash
# r9 Step 1 recipe: materialise the declared base (rc2, source/rc2-canonical fc9e196d) in a clean
# directory, apply the ordered patches from HANDOFF.json (none at Step 1), then run the one-command
# verifier with the expected identities. Host and build evidence only; never contacts an FM-1.
# usage (from the review repo root): materialize-and-test.sh NEW_OUT_DIR
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"; OUT="$1"; test ! -e "$OUT"; mkdir -p "$OUT"
M="$ROOT/HANDOFF.json"
BASE=$(python3 -I -c 'import json,sys;print(json.load(open(sys.argv[1]))["base"]["commit"])' "$M")
git -C "$ROOT" fetch -q origin source/rc2-canonical      # the published base branch
git init -q "$OUT/src"
git -C "$OUT/src" fetch -q "$ROOT" "$BASE"
git -C "$OUT/src" checkout -q --detach "$BASE"
python3 -I -c 'import json,sys
for p in sorted(json.load(open(sys.argv[1]))["patches"], key=lambda p: p["order"]): print(p["path"])' "$M" |
while read -r p; do git -C "$OUT/src" -c user.name=r9-recipe -c user.email=noreply@anthropic.com am -q "$ROOT/$p"; done
EXP() { python3 -I -c 'import json,sys;d=json.load(open(sys.argv[1]));print(eval(sys.argv[2]))' "$M" "$1"; }
python3 -I "$ROOT/claude-code/integration/verify_release.py" "$OUT/src" "$OUT/verify" \
  --expect-tree "$(EXP 'd["source"]["expected_tree_sha256"]')" \
  --expect-app "$(EXP 'd["artifacts"]["application"]["sha256"]')" \
  --expect-package "$(EXP 'd["artifacts"]["package"]["sha256"]')"
