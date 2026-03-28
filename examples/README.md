# Examples

## C

| Example | Purpose |
| --- | --- |
| [`po32_example.c`](po32_example.c) | Minimal roundtrip: build a frame, render it to float audio, decode it back, and print packet summaries |
| [`po32_demo.c`](po32_demo.c) | Full demo: roundtrip plus drum synthesis. Writes `demo_modem.wav` and `demo_kick.wav` |
| [`po32_pattern_editor.c`](po32_pattern_editor.c) | Interactive terminal pattern editor for one PO-32 pattern, with four trigger lanes per step, per-step accents, starter presets, and transfer export |
| [`po32_decode_capture.c`](po32_decode_capture.c) | Decode a transfer WAV back to normalized frame bytes, per-packet payload dumps, and a compact `pattern_summary.txt` |

## Build

From the repo root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/po32_example
./build/po32_demo
./build/po32_pattern_editor
./build/po32_decode_capture
```
