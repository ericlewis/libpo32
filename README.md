# libpo32

[![Tests](https://github.com/ericlewis/libpo32/actions/workflows/tests.yml/badge.svg)](https://github.com/ericlewis/libpo32/actions/workflows/tests.yml)
[![Commit Messages](https://github.com/ericlewis/libpo32/actions/workflows/commit-messages.yml/badge.svg)](https://github.com/ericlewis/libpo32/actions/workflows/commit-messages.yml)
[![PR Titles](https://github.com/ericlewis/libpo32/actions/workflows/pr-titles.yml/badge.svg)](https://github.com/ericlewis/libpo32/actions/workflows/pr-titles.yml)
[![Formatting](https://github.com/ericlewis/libpo32/actions/workflows/format.yml/badge.svg)](https://github.com/ericlewis/libpo32/actions/workflows/format.yml)
[![Static Analysis](https://github.com/ericlewis/libpo32/actions/workflows/static-analysis.yml/badge.svg)](https://github.com/ericlewis/libpo32/actions/workflows/static-analysis.yml)
[![Codecov](https://codecov.io/gh/ericlewis/libpo32/graph/badge.svg)](https://codecov.io/gh/ericlewis/libpo32)
[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fericlewis%2Flibpo32.svg?type=shield)](https://app.fossa.com/projects/git%2Bgithub.com%2Fericlewis%2Flibpo32?ref=badge_shield)

A small C99 library for Teenage Engineering PO-32 acoustic data transfers
and drum synthesis.

`libpo32` reimplements the PO-32 Tonic transfer stack and a compatible drum
voice model. It is not a full PO-32 firmware or UI emulator; it covers the
packet format, acoustic modem, frame decoder, and local drum synthesis needed
to build, send, receive, and preview PO-32 transfers.

## Contents

- [Overview](#overview)
- [Using It With a PO-32](#using-it-with-a-po-32)
- [Getting Started](#getting-started)
- [Examples](#examples)
- [Public API](#public-api)
- [Documentation](#documentation)
- [Project Layout](#project-layout)
- [Contributing](#contributing)
- [License](#license)

## Overview

| Area | What `libpo32` provides |
| --- | --- |
| Transfer protocol | Build and parse PO-32 patch, pattern, and state packets |
| Acoustic modem | Render transfer frames to DPSK audio for playback into the device |
| Decoder | Recover normalized frames and packets from transfer audio |
| Drum synthesis | Render the PO-32's 21-parameter drum voice locally for preview and testing |

The core is freestanding C99: no libc runtime, no external DSP libraries,
no platform audio APIs, no file I/O. Only the freestanding headers
`<stddef.h>` and `<stdint.h>` are used. Suitable for embedded targets and
bare-metal environments.

## Using It With a PO-32

The PO-32 is not receiving finished drum audio when you transfer a sound or a
pattern. It receives structured data:

| Data | Meaning |
| --- | --- |
| Patch packets | The two patch endpoints per instrument (`Left` and `Right`), with the destination instrument slot encoded in the packet |
| Pattern packets | Which instruments trigger on which steps, with the destination pattern slot encoded as `pattern_number` |
| State packet | Tempo, swing, morph defaults, and the transferred pattern list |

The device then uses its own internal synth engine to turn those parameters
into sound.

At a high level, the workflow with real hardware is:

| Step | What you do |
| --- | --- |
| 1 | Build a transfer in software from patch, pattern, and state data |
| 2 | Render that transfer to a WAV or live audio stream |
| 3 | Put the PO-32 into its normal receive/import flow |
| 4 | Play the rendered transfer audio into the device |
| 5 | The PO-32 decodes the packets and writes them to the sound or pattern slots named inside those packets |

## Getting Started

From the repository root, configure, build, run tests, then the demo:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/po32_demo
```

Optional, but recommended for local development:

```sh
./scripts/install-git-hooks.sh
```

That enables the repo-managed pre-commit hook, which runs `clang-format` on
staged `.c` and `.h` files before each commit and validates commit subjects
against Conventional Commits.

The demo builds a transfer frame, renders it to audio, decodes it back
(verifying a lossless roundtrip), synthesizes a drum hit, and writes two
WAV files: `demo_modem.wav` and `demo_kick.wav`.

## Examples

| Command | Purpose |
| --- | --- |
| `./build/po32_demo` | End-to-end demo: transfer frame build/render/decode plus local drum synthesis |
| `./build/po32_pattern_editor` | Interactive PO-32 pattern editing and WAV export |
| `./build/po32_decode_capture <input.wav> <out-dir>` | Packet and pattern dumps from a transfer WAV |

## Public API

| Header | Responsibility |
| --- | --- |
| [`include/po32.h`](include/po32.h) | Transfer builder, packet helpers, renderer, and decoder |
| [`include/po32_synth.h`](include/po32_synth.h) | Drum synthesizer |

## Documentation

| Document | Purpose |
| --- | --- |
| [Architecture](docs/ARCHITECTURE.md) | How the codec works |
| [Protocol](docs/PROTOCOL.md) | Wire format details |
| [C API](docs/C_API.md) | C function reference |
| [Synth](docs/SYNTH.md) | Synthesizer signal path |
| [Patch Parameters](docs/PATCH_PARAMETERS.md) | The 21 parameters |
| [Examples](examples/README.md) | Supported example programs |
| [Contributing](CONTRIBUTING.md) | Local setup, checks, and commit conventions |

## Project Layout

| Path | Purpose |
| --- | --- |
| [`src`](src) | Core implementation |
| [`include`](include) | Public headers |
| [`examples`](examples) | Supported C examples |
| [`tests`](tests) | Core test coverage |
| [`docs`](docs) | Architecture, protocol, and API notes |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local setup, verification commands,
and the Conventional Commits policy used for SemVer-friendly history.

## License

Copyright (c) 2026 Eric Lewis. Licensed under the MIT License. See [LICENSE](LICENSE).


[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fericlewis%2Flibpo32.svg?type=large)](https://app.fossa.com/projects/git%2Bgithub.com%2Fericlewis%2Flibpo32?ref=badge_large)