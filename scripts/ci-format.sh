#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

: "${CLANG_FORMAT:=clang-format}"

find "$root_dir" -maxdepth 3 \
  \( -path "$root_dir/.git" -o -path "$root_dir/build*" \) -prune -o \
  -type f \( -name '*.c' -o -name '*.h' \) -print0 |
  xargs -0 "$CLANG_FORMAT" --dry-run -Werror
