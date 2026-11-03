#!/bin/sh
set -eu
: "${WASM_LD:=wasm-ld-14}"
"$WASM_LD" --no-entry --allow-undefined --export-dynamic \
 --global-base=50331648 --initial-memory=134217728 --max-memory=268435456 \
 -z stack-size=1048576 "$1/felucca.o" "$1/whost.o" "$1"/obj/*.o -o "$1/bigbeat.wasm"
