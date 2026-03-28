# libpo32

A small C99 library for Teenage Engineering PO-32 acoustic data transfers
and drum synthesis.

`libpo32` reimplements the software side of the PO-32 Tonic data path:

- the packet format used for patch, pattern, and state transfers
- the acoustic DPSK modem used to turn those packets into audio
- the corresponding decoder that reconstructs normalized frames from transfer audio
- a compatible drum synth that renders the PO-32's 21 patch parameters locally

It is not a full PO-32 firmware or UI emulator. It is the transfer stack and
the drum-voice model that sit around the device's own synth engine.

## What The PO-32 Receives

The PO-32 is not receiving finished drum audio when you transfer a sound or a
pattern. It receives structured data:

| Data | Meaning |
| --- | --- |
| Patch packets | The two patch endpoints per instrument (`Left` and `Right`), with the destination instrument slot encoded in the packet |
| Pattern packets | Which instruments trigger on which steps, with the destination pattern slot encoded as `pattern_number` |
| State packet | Tempo, swing, morph defaults, and the transferred pattern list |

The device then uses its own internal synth engine to turn those parameters
into sound.

That means `libpo32` is doing two related jobs:

| Area | What `libpo32` provides |
| --- | --- |
| Transfer protocol | Build, parse, render, and decode the PO-32 acoustic transfer format |
| Drum synthesis | Render the 21-parameter drum voice locally so patches can be previewed and tested off-device |

## What It Does

- **Build** PO-32 transfer frames from typed packets
- **Render** frames to mono float audio (DPSK modulation)
- **Decode** mono float audio back to frames
- **Synthesize** drum sounds from the 21 patch parameters

The core is freestanding C99: no libc runtime, no external DSP libraries,
no platform audio APIs, no file I/O. Only the freestanding headers
`<stddef.h>` and `<stdint.h>` are used. Suitable for embedded targets and
bare-metal environments.

## Using It With A PO-32

At a high level, the workflow with real hardware is:

| Step | What you do |
| --- | --- |
| 1 | Build a transfer in software from patch, pattern, and state data |
| 2 | Render that transfer to a WAV or live audio stream |
| 3 | Put the PO-32 into its normal receive/import flow |
| 4 | Play the rendered transfer audio into the device |
| 5 | The PO-32 decodes the packets and writes them to the sound or pattern slots named inside those packets |

## Getting started

From the repository root, configure, build, run tests, then the demo:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/po32_demo
```

The demo builds a transfer frame, renders it to audio, decodes it back
(verifying a lossless roundtrip), synthesizes a drum hit, and writes two
WAV files: `demo_modem.wav` and `demo_kick.wav`.

Other supported examples include:

| Command | Purpose |
| --- | --- |
| `./build/po32_pattern_editor` | Interactive PO-32 pattern editing and WAV export |
| `./build/po32_decode_capture <input.wav> <out-dir>` | Packet and pattern dumps from a transfer WAV |

## Public API

| Header | Responsibility |
| --- | --- |
| [`include/po32.h`](include/po32.h) | Transfer builder, packet helpers, renderer, and decoder |
| [`include/po32_synth.h`](include/po32_synth.h) | Drum synthesizer |

## Project Layout

| Path | Purpose |
| --- | --- |
| [`src`](src) | Core implementation |
| [`include`](include) | Public headers |
| [`examples`](examples) | Supported C examples |
| [`tests`](tests) | Core test coverage |
| [`docs`](docs) | Architecture, protocol, and API notes |

## Documentation

| Document | Purpose |
| --- | --- |
| [Architecture](docs/ARCHITECTURE.md) | How the codec works |
| [Protocol](docs/PROTOCOL.md) | Wire format details |
| [C API](docs/C_API.md) | C function reference |
| [Synth](docs/SYNTH.md) | Synthesizer signal path |
| [Patch Parameters](docs/PATCH_PARAMETERS.md) | The 21 parameters |
| [Examples](examples/README.md) | Supported example programs |

## License

Copyright (c) 2026 Eric Lewis. Licensed under the MIT License. See [LICENSE](LICENSE).
