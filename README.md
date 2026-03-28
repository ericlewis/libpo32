# libpo32

A small C99 library for Teenage Engineering PO-32 acoustic data transfers
and drum synthesis.

## What It Does

- **Build** PO-32 transfer frames from typed packets
- **Render** frames to mono float audio (DPSK modulation)
- **Decode** mono float audio back to frames
- **Synthesize** drum sounds from the 21 patch parameters

The core is freestanding C99: no libc dependency beyond `<stddef.h>` and
`<stdint.h>`, no external DSP libraries, no platform audio APIs, no file
I/O. Suitable for embedded targets and bare-metal environments.

## Quick Start

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/po32_demo
```

The demo builds a transfer frame, renders it to audio, decodes it back
(verifying a lossless roundtrip), synthesizes a drum hit, and writes two
WAV files: `demo_modem.wav` and `demo_kick.wav`.

Other supported examples include:

- `./build/po32_pattern_editor` for interactive PO-32 pattern editing and WAV export
- `./build/po32_decode_capture <input.wav> <out-dir>` for packet and pattern dumps

## Public API

- [`include/po32.h`](include/po32.h) — transfer builder, packet
  helpers, renderer, and decoder
- [`include/po32_synth.h`](include/po32_synth.h) — drum synthesizer

## Project Layout

- [`src`](src) — core implementation
- [`include`](include) — public headers
- [`examples`](examples) — supported C examples
- [`tests`](tests) — core test coverage
- [`docs`](docs) — architecture, protocol, and API notes

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — how the codec works
- [Protocol](docs/PROTOCOL.md) — wire format details
- [C API](docs/C_API.md) — C function reference
- [Synth](docs/SYNTH.md) — synthesizer signal path
- [Patch Parameters](docs/PATCH_PARAMETERS.md) — the 21 parameters
- [Examples](examples/README.md) — supported example programs

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

## License

MIT — see [LICENSE](LICENSE).
