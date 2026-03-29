#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build_dir="$root_dir/build-static-analysis"

: "${CC:=clang}"

cppcheck \
  --enable=warning,performance,portability \
  --std=c99 \
  --error-exitcode=1 \
  --inline-suppr \
  --suppress=missingIncludeSystem \
  -I "$root_dir/core/include" \
  "$root_dir/core/src" \
  "$root_dir/core/tests" \
  "$root_dir/core/examples"

scan-build --status-bugs \
  cmake -S "$root_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="$CC"

scan-build --status-bugs cmake --build "$build_dir" -j4
