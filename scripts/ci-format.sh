#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

. "$root_dir/scripts/resolve-clang-format.sh"

find "$root_dir" -maxdepth 4 \
  \( -path "$root_dir/.git" -o -path "$root_dir/build*" \) -prune -o \
  -type f \( -name '*.c' -o -name '*.h' \) -print0 |
  xargs -0 "$CLANG_FORMAT" -style=file --dry-run -Werror
