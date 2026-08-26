#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
mode=${1:-core}

run_core() {
  cmake -S "$root_dir" -B "$root_dir/build-release-check" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$root_dir/build-release-check" -j4
  ctest --test-dir "$root_dir/build-release-check" --output-on-failure
}

run_sanitize() {
  cmake -S "$root_dir" -B "$root_dir/build-sanitize-check" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DPO32_SANITIZE=ON
  cmake --build "$root_dir/build-sanitize-check" -j4
  ctest --test-dir "$root_dir/build-sanitize-check" --output-on-failure
}

case "$mode" in
  core)
    run_core
    ;;
  release)
    run_core
    ;;
  sanitize)
    run_sanitize
    ;;
  *)
    printf 'usage: %s [core|release|sanitize]\n' "$0" >&2
    exit 64
    ;;
esac
