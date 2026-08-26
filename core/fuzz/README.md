# Fuzzing

libFuzzer harnesses for every surface that consumes untrusted input, plus
differential checks that the builder, parser, modulator, and demodulator
always agree.

| Harness | Target | Key properties checked |
| --- | --- | --- |
| `po32_fuzz_frame_parse` | `po32_frame_parse()` | No crashes on arbitrary frames; accepted packets satisfy offset/length invariants; typed decoders never crash on parser-approved payloads |
| `po32_fuzz_packet_decode` | `po32_packet_decode()` / `po32_packet_encode()` | decode∘encode is idempotent for every tag; decoded fields stay in range |
| `po32_fuzz_patch_import` | `po32_patch_parse_mtdrum_text()` | No out-of-bounds reads; parsed parameters are finite and in [0, 1]; byte codec round trip is stable |
| `po32_fuzz_synth_render` | `po32_synth_render()` | Rendering in-contract patches never crashes; every sample is a real number in [-1, 1] |
| `po32_fuzz_audio_decode` | `po32_decode_f32()` | No crashes on arbitrary float audio (NaN and infinity included); any reconstructed frame re-parses cleanly |
| `po32_fuzz_modem_roundtrip` | full pipeline | Built frames parse back to exactly the packets that went in; render → decode is lossless at 22050/44100/48000 Hz |

## Running locally

Requires Clang with the libFuzzer runtime
(`apt install clang libclang-rt-<N>-dev` on Debian/Ubuntu).

```sh
./scripts/ci-fuzz.sh                       # build, seed, smoke-fuzz every target
PO32_FUZZ_SECONDS=600 ./scripts/ci-fuzz.sh # longer campaign
```

Or drive one target by hand:

```sh
cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DPO32_BUILD_FUZZERS=ON
cmake --build build-fuzz -j4
./build-fuzz/core/po32_fuzz_seed_gen build-fuzz/corpus
./build-fuzz/core/po32_fuzz_frame_parse build-fuzz/corpus/frame_parse
```

## Replaying crashes without Clang

Build with `-DPO32_FUZZ_STANDALONE=ON` (any C compiler) to get file-driven
binaries that replay corpus or crash files:

```sh
./build/core/po32_fuzz_frame_parse crash-<hash>
```

## Adding a harness

1. Create `core/fuzz/fuzz_<name>.c` defining `LLVMFuzzerTestOneInput()` and
   include `fuzz_driver.h`.
2. Add `<name>` to `PO32_FUZZ_TARGETS` in `core/CMakeLists.txt`.
3. Teach `fuzz_seed_gen.c` to emit at least one valid seed for it.

Assert invariants with `fuzz_check()`; prefer strong differential
properties (round trips, idempotence, cross-checking two implementations)
over "does not crash".
