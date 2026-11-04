# libpo

[![Tests](https://github.com/ericlewis/libpo32/actions/workflows/tests.yml/badge.svg?branch=main&event=push)](https://github.com/ericlewis/libpo32/actions/workflows/tests.yml)
[![Codecov](https://codecov.io/gh/ericlewis/libpo32/branch/main/graph/badge.svg)](https://codecov.io/gh/ericlewis/libpo32)
[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fericlewis%2Flibpo32.svg?type=shield)](https://app.fossa.com/projects/git%2Bgithub.com%2Fericlewis%2Flibpo32?ref=badge_shield)

Freestanding C99 transfer codecs and sound engines for Teenage Engineering
Pocket Operators. Model-family APIs share framing, stereo modem and numerical
primitives while keeping drum synthesis, sample pools and speech records distinct.

## Contents

- [Overview](#overview)
- [Using It With a PO-32](#using-it-with-a-po-32)
- [Getting Started](#getting-started)
- [Examples](#examples)
- [Projects Built with libpo32](#projects-built-with-libpo32)
- [Public API](#public-api)
- [Documentation](#documentation)
- [Project Layout](#project-layout)
- [Contributing](#contributing)
- [License](#license)

## Overview

| Family | Transport | Content and sound |
| --- | --- | --- |
| PO-32 | Mono DPSK encode/decode | Patches, patterns and drum synthesis |
| PO-33 / PO-133 | Stereo DQPSK encode/decode | PCM sample conversion, settings, patterns and template editing |
| PO-35 / PO-137 | Stereo DQPSK encode/decode | Drum patches, speech records, PCM analysis and approximate speech synthesis |

The core uses caller-owned memory and standard freestanding C99 headers, with
no heap allocation or calls to libc, libm, filesystem or device APIs. On Unix,
noninstrumented builds include a CTest check of the built archive for unresolved
symbols. Compiler runtime requirements depend on the target; see
[Architecture](core/docs/ARCHITECTURE.md#freestanding-contract). Hosted
applications own allocation, WAV files and audio playback.

[PO-33 editing](core/examples/README.md#po-33--po-133-template-editing) accepts
different PCM rates and lengths while preserving sample headers and references.
Short inputs are padded; longer inputs grow the stored pool without extending
existing trims. [PO-35 speech editing](core/examples/README.md#po-35--po-137-speech-replacement)
analyzes PCM into new speech records for existing slots while retaining their
metadata and controls. Speech analysis and rendering use an approximate model,
not the original Bitspeek or device implementation. Unknown fields remain raw.
Generated stereo-family transfers have not been validated on physical hardware.

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
./build/core/po32_demo
```

Optional, but recommended for local development:

```sh
./scripts/install-git-hooks.sh
```

That enables the repo-managed pre-commit hook, which runs `clang-format` on
staged `.c` and `.h` files before each commit and validates commit subjects
against Conventional Commits. The repo-managed `pre-push` hook runs the static
analysis script before push.

The pre-commit hook requires `clang-format` 18 and fails on any other major
version; see [CONTRIBUTING.md](CONTRIBUTING.md#clang-format) for install options.

The demo builds a transfer frame, renders it to audio, decodes it back
(verifying a lossless roundtrip), synthesizes a drum hit, and writes two
WAV files: `demo_modem.wav` and `demo_kick.wav`.

### Installing and Consuming

`cmake --install` ships the static library, headers, a CMake package, and a
pkg-config file, so both consumption paths work out of the box:

```cmake
find_package(LibPO 0.2 REQUIRED)
target_link_libraries(app PRIVATE LibPO::po)
```

```sh
cc app.c $(pkg-config --cflags --libs po)
```

The legacy `LibPO32::po32` and `pkg-config po32` interfaces remain available.
The build target is `po32`, producing `libpo32.a` on Unix. Existing PO-32 public
names, layouts and status values are preserved.

## Examples

| Command | Purpose |
| --- | --- |
| `./build/core/po32_example` | Minimal roundtrip: encode, render, decode, and verify |
| `./build/core/po32_demo` | End-to-end demo: transfer frame build/render/decode plus local drum synthesis |
| `./build/core/po32_pattern_editor` | Interactive PO-32 pattern editing and WAV export |
| `./build/core/po32_decode_capture <input.wav> <out-dir>` | Packet and pattern dumps from a transfer WAV |

Additional hosted tools:

| Command | Purpose |
| --- | --- |
| `./build/core/po33_decode_capture input.wav new-directory` | PO-33/133 raw records and sample WAV export |
| `./build/core/po33_encode_capture frame.bin new.wav` | PO-33/133 template re-encoding and explicit edits |
| `./build/core/po35_decode_capture input.wav new-directory` | PO-35/137 raw records and speech previews |
| `./build/core/po35_encode_capture frame.bin new.wav` | PO-35/137 frame-to-modem encoding, with optional `--speech SLOT audio.wav` replacements |

## Projects Built with libpo32

| Project | What it does |
| --- | --- |
| [Libpo32 for Schwung](https://github.com/mestela/schwung-libpo32) | A 16-voice drum synthesizer module for Schwung on Ableton Move, powered by `libpo32` |

## Public API

| Header | Responsibility |
| --- | --- |
| [`core/include/po.h`](core/include/po.h) | Optional umbrella include |
| [`core/include/po_common.h`](core/include/po_common.h) | Shared statuses and raw packets |
| [`core/include/po33.h`](core/include/po33.h) | Sampler-family data and framing |
| [`core/include/po35.h`](core/include/po35.h) | Speech-family data and framing |
| [`core/include/po35_analysis.h`](core/include/po35_analysis.h) | Caller-workspace PCM-to-speech analysis |
| [`core/include/po35_synth.h`](core/include/po35_synth.h) | Caller-owned native speech renderer |
| [`core/include/po_stereo.h`](core/include/po_stereo.h) | Shared chunked stereo TX and explicit offline RX |
| [`core/include/po32.h`](core/include/po32.h) | Transfer builder, packet helpers, renderer, and decoder |
| [`core/include/po32_synth.h`](core/include/po32_synth.h) | Drum synthesizer |

## Documentation

| Document | Purpose |
| --- | --- |
| [Architecture](core/docs/ARCHITECTURE.md) | Core and application boundaries |
| [Protocol](core/docs/PROTOCOL.md) | PO-32 wire format |
| [Format families](core/docs/PO_FORMATS.md) | PO-33/35/133/137 layouts and evidence limits |
| [C API](core/docs/C_API.md) | C function reference |
| [Synth](core/docs/SYNTH.md) | Synthesizer signal path |
| [Patch Parameters](core/docs/PATCH_PARAMETERS.md) | The 21 parameters |
| [Bindings](core/docs/BINDINGS.md) | PO-32 binding conventions |
| [Examples](core/examples/README.md) | Supported example programs |
| [Fuzzing](core/fuzz/README.md) | Fuzz harnesses and differential properties |
| [Contributing](CONTRIBUTING.md) | Local setup, checks, and commit conventions |

## Project Layout

| Path | Purpose |
| --- | --- |
| [`core/src`](core/src) | Core C implementation |
| [`core/include`](core/include) | Public C headers |
| [`core/examples`](core/examples) | Supported C examples |
| [`core/tests`](core/tests) | Core test coverage |
| [`core/fuzz`](core/fuzz) | Existing PO-32 libFuzzer harnesses |
| [`core/docs`](core/docs) | Architecture, protocol, and API notes |
| [`bindings/go`](bindings/go) | Existing PO-32 Go bindings (cgo) |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local setup, verification commands,
and the Conventional Commits policy used for SemVer-friendly history.

## License

Copyright (c) 2026 Eric Lewis. Licensed under the MIT License. See [LICENSE](LICENSE).
