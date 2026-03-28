#!/bin/sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
VERSION=$(tr -d '\n' < "$ROOT/VERSION")

check_contains() {
  file=$1
  needle=$2

  if ! grep -Fq "$needle" "$ROOT/$file"; then
    printf 'version mismatch in %s: expected %s\n' "$file" "$needle" >&2
    exit 1
  fi
}

check_contains "CMakeLists.txt" "file(STRINGS \"\${CMAKE_CURRENT_SOURCE_DIR}/VERSION\" PO32_VERSION LIMIT_COUNT 1)"
check_contains "CMakeLists.txt" "project(libpo32 VERSION \${PO32_VERSION} LANGUAGES C)"
check_contains "CHANGELOG.md" "## [${VERSION}]"

printf 'Core release metadata matches %s\n' "$VERSION"
