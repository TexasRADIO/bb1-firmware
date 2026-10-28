#!/bin/bash
# Run build_from_source.py in the pinned Jieli container. Every object is built from SOURCE.
# usage: run_build.sh SOURCE_TREE CMDS_DIR RECIPE_DIR OUT_DIR   (OUT_DIR must not exist)
set -euo pipefail
SRC="$1"; CMDS="$2"; RECIPE="$3"; OUT="$4"
HERE="$(cd "$(dirname "$0")" && pwd)"; WS="${BB_WORKSPACE:-/Volumes/Orange Base/Codex Workspaces/big-beat-claude-integration}"
OB="/Volumes/Orange Base"
# Runtime image: build/image/Dockerfile (Debian bookworm pinned digest + python3). The original
# sha256:2d0e361d... image was lost when Docker storage was reset on 27 Oct; this one reproduces boot42.
IMAGE="${IMAGE:-bb-build-runtime:bookworm-r1}"
# Linker overlay: a recipe may carry its own (RECIPE/link, in Git); otherwise the pinned run-r3 overlay.
LINK="$OB/Codex Working Cache/tmp/sample-audio-r758/linker-overlay"; [ -d "$RECIPE/link" ] && LINK="$(cd "$RECIPE/link" && pwd)"
test ! -e "$OUT"; mkdir -p "$OUT"; mkdir -p "$WS/work/empty-generated"
"${DOCKER:-/Users/loudounhq/.docker/bin/docker}" run --rm --platform linux/amd64 --network none --cpus 4 --memory 2g \
 --mount "type=bind,source=$SRC,target=/src,readonly" \
 --mount "type=bind,source=$CMDS,target=/cmds,readonly" \
 --mount "type=bind,source=$RECIPE,target=/recipe,readonly" \
 --mount "type=bind,source=$WS/work/tools,target=/tools,readonly" \
 --mount "type=bind,source=$LINK,target=/link,readonly" \
 --mount "type=bind,source=$OB/Codex Workspaces/big-beat-fm1-architecture-20261015/experiments/bb-909-shared-assets.W3nC8U,target=/assets,readonly" \
 --mount "type=bind,source=$WS/work/empty-generated,target=/generated,readonly" \
 --mount "type=bind,source=$OB/Codex Workspaces/big-beat-fm1/tools/jieli-linux-toolchains-20260730.1,target=/opt/jieli,readonly" \
 --mount "type=bind,source=$HERE/build_from_source.py,target=/build.py,readonly" \
 --mount "type=bind,source=$OUT,target=/out" \
 "$IMAGE" python3 /build.py
