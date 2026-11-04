#!/bin/sh
# Keep the standalone Go module's generated C bundle identical to core/.
set -eu
root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bundle_dir="$root_dir/bindings/go/internal/ccore"
manifest="$bundle_dir/SOURCES.txt"
mode=${1:-sync}
case "$mode" in
  sync|--check) ;;
  *) printf 'usage: %s [--check]\n' "$0" >&2; exit 64 ;;
esac
failed=0
while IFS= read -r relative; do
  case "$relative" in
    LICENSE) source="$root_dir/LICENSE" ;;
    include/*.h|src/*.c|src/*.h) source="$root_dir/core/$relative" ;;
    *) printf 'Invalid C bundle entry: %s\n' "$relative" >&2; exit 1 ;;
  esac
  destination="$bundle_dir/$relative"
  if [ "$mode" = --check ]; then
    if [ -L "$destination" ] || ! cmp -s "$source" "$destination"; then
      printf 'Go C bundle differs: %s\n' "$relative" >&2
      failed=1
    fi
  else
    mkdir -p "$(dirname "$destination")"
    if [ -L "$destination" ]; then
      printf 'Refusing to overwrite symlink: %s\n' "$destination" >&2
      exit 1
    fi
    cp "$source" "$destination"
  fi
done < "$manifest"
# The module license must also be present at its root for module consumers.
module_license="$root_dir/bindings/go/LICENSE"
if [ "$mode" = --check ]; then
  if [ -L "$module_license" ] || ! cmp -s "$root_dir/LICENSE" "$module_license"; then
    printf 'Go module license differs.\n' >&2
    failed=1
  fi
else
  if [ -L "$module_license" ]; then
    printf 'Refusing to overwrite module license symlink.\n' >&2
    exit 1
  fi
  cp "$root_dir/LICENSE" "$module_license"
fi
for destination in "$bundle_dir"/include/* "$bundle_dir"/src/*; do
  [ -e "$destination" ] || [ -L "$destination" ] || continue
  relative=${destination#"$bundle_dir/"}
  if ! grep -Fxq "$relative" "$manifest"; then
    printf 'Unlisted Go C bundle file: %s\n' "$relative" >&2
    failed=1
  fi
done
if [ "$failed" -ne 0 ]; then
  exit 1
fi
printf 'Go C bundle matches its source manifest.\n'
