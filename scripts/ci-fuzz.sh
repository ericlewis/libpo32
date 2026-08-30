#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build_dir="$root_dir/build-fuzz"

: "${CC:=clang}"
: "${PO32_FUZZ_SECONDS:=20}"

cmake -S "$root_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER="$CC" \
  -DPO32_BUILD_FUZZERS=ON
cmake --build "$build_dir" -j4

corpus_root="$build_dir/corpus"
mkdir -p "$corpus_root"
"$build_dir/core/po32_fuzz_seed_gen" "$corpus_root"

for target in frame_parse packet_decode patch_import synth_render audio_decode modem_roundtrip; do
  mkdir -p "$corpus_root/$target"
  printf '\n=== fuzzing %s for %ss ===\n' "$target" "$PO32_FUZZ_SECONDS"
  "$build_dir/core/po32_fuzz_$target" "$corpus_root/$target" \
    -max_total_time="$PO32_FUZZ_SECONDS" \
    -print_final_stats=1 \
    -rss_limit_mb=1024
done

printf '\nAll fuzz targets completed without findings\n'
