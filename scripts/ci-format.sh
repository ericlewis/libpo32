#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

. "$root_dir/scripts/resolve-clang-format.sh"

# Use the source inventory, not ignored build output or generated caches.
cd "$root_dir"
git ls-files --cached --others --exclude-standard -z -- '*.c' '*.h' |
  xargs -0 "$CLANG_FORMAT" -style=file --dry-run -Werror
