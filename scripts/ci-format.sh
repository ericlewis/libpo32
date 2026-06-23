#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

if [ -z "${CLANG_FORMAT:-}" ]; then
  if command -v clang-format-18 >/dev/null 2>&1; then
    CLANG_FORMAT=clang-format-18
  else
    CLANG_FORMAT=clang-format
  fi
fi

find "$root_dir" -maxdepth 4 \
  \( -path "$root_dir/.git" -o -path "$root_dir/build*" \) -prune -o \
  -type f \( -name '*.c' -o -name '*.h' \) -print0 |
  xargs -0 "$CLANG_FORMAT" -style=file --dry-run -Werror
