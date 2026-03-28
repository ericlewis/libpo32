#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
mode=${1:-core}

run_core() {
  cmake -S "$root_dir" -B "$root_dir/build-release-check" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$root_dir/build-release-check" -j4
  ctest --test-dir "$root_dir/build-release-check" --output-on-failure
}

case "$mode" in
  core)
    run_core
    ;;
  release)
    run_core
    ;;
  *)
    printf 'usage: %s [core|release]\n' "$0" >&2
    exit 64
    ;;
esac
