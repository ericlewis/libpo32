#!/bin/sh
# With an archive argument, audit the actual build product. Otherwise compile
# sources under the documented freestanding flags and audit those objects.
set -eu
audit_root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
audit_cc=${1:-cc}
audit_nm=${2:-nm}
audit_archive=${3:-}
audit_ar=${4:-ar}
audit_dir=$(mktemp -d "${TMPDIR:-/tmp}/libpo-freestanding.XXXXXX")
trap 'rm -rf "$audit_dir"' EXIT HUP INT TERM
set --
audit_count=0
if test -n "$audit_archive"; then
  test -f "$audit_archive"
  audit_archive=$(CDPATH= cd -- "$(dirname "$audit_archive")" && pwd)/$(basename "$audit_archive")
  (cd "$audit_dir" && "$audit_ar" x "$audit_archive")
  for audit_object in "$audit_dir"/*.o; do
    test -f "$audit_object"
    set -- "$@" "$audit_object"
    audit_count=$((audit_count + 1))
  done
else
  for audit_source in "$audit_root"/core/src/*.c; do
    audit_object="$audit_dir/$(basename "$audit_source" .c).o"
    "$audit_cc" -std=c99 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
      -I"$audit_root/core/include" -c "$audit_source" -o "$audit_object"
    set -- "$@" "$audit_object"
    audit_count=$((audit_count + 1))
  done
fi
"$audit_cc" -r -nostdlib "$@" -o "$audit_dir/combined.o"
"$audit_nm" -u "$audit_dir/combined.o" > "$audit_dir/undefined.txt"
if test -s "$audit_dir/undefined.txt"; then
  cat "$audit_dir/undefined.txt" >&2
  printf '%s\n' 'Freestanding core has external runtime references.' >&2
  exit 1
fi
printf 'Freestanding core: %s translation units, no undefined symbols.\n' "$audit_count"
