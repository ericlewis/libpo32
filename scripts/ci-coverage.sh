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
  -DCMAKE_C_FLAGS="--coverage -O0 -g" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage"

cmake --build "$build_dir" -j4
ctest --test-dir "$build_dir" --output-on-failure --output-junit "$report_dir/junit.xml"

gcovr \
  --root "$root_dir" \
  --filter "$root_dir/src/" \
  --filter "$root_dir/examples/" \
  --exclude "$root_dir/tests/" \
  --print-summary

gcovr \
  --root "$root_dir" \
  --filter "$root_dir/src/" \
  --filter "$root_dir/examples/" \
  --exclude "$root_dir/tests/" \
  --xml-pretty \
  --output "$report_dir/cobertura.xml"

gcovr \
  --root "$root_dir" \
  --filter "$root_dir/src/" \
  --filter "$root_dir/examples/" \
  --exclude "$root_dir/tests/" \
  --html-details \
  --output "$report_dir/index.html"

gcovr \
  --root "$root_dir" \
  --filter "$root_dir/src/" \
  --filter "$root_dir/examples/" \
  --exclude "$root_dir/tests/" \
  --txt \
  --output "$report_dir/summary.txt"

gcovr \
  --root "$root_dir" \
  --filter "$root_dir/src/" \
  --filter "$root_dir/examples/" \
  --exclude "$root_dir/tests/" \
  --markdown \
  --output "$report_dir/summary.md"
