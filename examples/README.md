# Examples

## C

- [`po32_example.c`](po32_example.c) — Minimal roundtrip: build a frame,
  render it to float audio, decode it back, and print packet summaries.

- [`po32_demo.c`](po32_demo.c) — Full demo: roundtrip + drum synthesis.
  Writes `demo_modem.wav` and `demo_kick.wav`.

## Build

From the repo root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/po32_example
./build/po32_demo
```
