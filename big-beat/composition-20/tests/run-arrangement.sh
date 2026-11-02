#!/bin/bash
set -eu
source_root=$(cd "$(dirname "$0")/../../.." && pwd)
output_root=${1:?Usage: run-arrangement.sh EXISTING_OUTPUT_DIRECTORY}
[ -d "$output_root" ]
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -g -I"$source_root/firmware/src/bb" \
  "$source_root/firmware/src/bb/bb_arrangement.c" \
  "$source_root/big-beat/composition-20/tests/arrangement_test.c" \
  -o "$output_root/arrangement-test"
"$output_root/arrangement-test"
