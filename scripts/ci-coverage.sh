#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build_dir="$root_dir/build-coverage"
report_dir=${1:-"$root_dir/coverage"}

: "${CC:=gcc}"

rm -rf "$build_dir"
mkdir -p "$report_dir"

cmake -S "$root_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER="$CC" \
  -DPO32_COVERAGE=ON

cmake --build "$build_dir" -j4
ctest --test-dir "$build_dir" --output-on-failure --output-junit "$report_dir/junit.xml"

# Internal tests compile source under renamed entry points. Aggregate each
# physical source line across those instances instead of reporting duplicates.
gcovr \
  --merge-lines \
  --root "$root_dir" \
  --filter "$root_dir/core/src/" \
  --filter "$root_dir/core/examples/" \
  --exclude "$root_dir/core/tests/" \
  --print-summary

gcovr \
  --merge-lines \
  --root "$root_dir" \
  --filter "$root_dir/core/src/" \
  --filter "$root_dir/core/examples/" \
  --exclude "$root_dir/core/tests/" \
  --xml-pretty \
  --output "$report_dir/cobertura.xml"

gcovr \
  --merge-lines \
  --root "$root_dir" \
  --filter "$root_dir/core/src/" \
  --filter "$root_dir/core/examples/" \
  --exclude "$root_dir/core/tests/" \
  --html-details \
  --output "$report_dir/index.html"

gcovr \
  --merge-lines \
  --root "$root_dir" \
  --filter "$root_dir/core/src/" \
  --filter "$root_dir/core/examples/" \
  --exclude "$root_dir/core/tests/" \
  --txt \
  --output "$report_dir/summary.txt"

gcovr \
  --merge-lines \
  --root "$root_dir" \
  --filter "$root_dir/core/src/" \
  --filter "$root_dir/core/examples/" \
  --exclude "$root_dir/core/tests/" \
  --markdown \
  --output "$report_dir/summary.md"
