# libpo

[![Tests](https://github.com/ericlewis/libpo/actions/workflows/tests.yml/badge.svg?branch=main&event=push)](https://github.com/ericlewis/libpo/actions/workflows/tests.yml)
[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fericlewis%2Flibpo32.svg?type=shield)](https://app.fossa.com/projects/git%2Bgithub.com%2Fericlewis%2Flibpo32?ref=badge_shield)

Decode, edit and encode Teenage Engineering Pocket Operator backups, convert
samples, and synthesize drum and speech audio. libpo provides a freestanding C99
core, command-line WAV tools, and one Go module covering all supported families.

## Supported families

| Devices | Backup transport | Data and audio APIs |
| --- | --- | --- |
| PO-32 tonic | Mono DPSK encode/decode | Drum patches, patterns, state and drum synthesis |
| PO-33 K.O! / PO-133 Street Fighter | Stereo DQPSK encode/decode | Sample conversion, patterns, settings and sample-pool editing |
| PO-35 speak / PO-137 Rick and Morty | Stereo DQPSK encode/decode | Speech analysis and synthesis, drum patches, patterns and settings |

The C core uses caller-owned memory, with no heap allocation or calls to libc,
libm, filesystem or device APIs. Hosted tools handle WAV files and allocation;
applications handle playback and device I/O. See the
[freestanding contract](core/docs/ARCHITECTURE.md#freestanding-contract) for
compiler and target requirements.

PO-35/137 speech analysis and synthesis are approximate models. Some pattern
and metadata fields remain uninterpreted and are preserved as raw bytes.
Software loopbacks verify transfer-byte preservation; generated stereo-family
backups and speech output have not been validated on physical devices.

## Build and try

Requires CMake 3.16 or newer and a C99 compiler. Go is optional.

```sh
git clone https://github.com/ericlewis/libpo.git
cd libpo
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
(cd build && ctest --output-on-failure)
(cd build && ./core/po32_demo)
```

The demo builds a PO-32 transfer, renders and decodes it, verifies the roundtrip,
and synthesizes a drum hit. It writes `build/demo_modem.wav` and
`build/demo_kick.wav`.

## Work with backups

The stereo tools read PCM16, PCM24, PCM32 or float32 WAV captures at 32–192 kHz.
Keep both channels: mixing a backup to mono loses data. Output directories and
WAV files must not already exist. The PO-33/35 edit selectors below are
zero-based.

### PO-33 / PO-133 samples

Extract a backup, rebuild it, and check that the decoded frame is unchanged:

```sh
./build/core/po33_decode_capture backup.wav decoded-po33
./build/core/po33_encode_capture decoded-po33/frame.bin rebuilt-po33.wav
./build/core/po33_decode_capture rebuilt-po33.wav checked-po33
cmp decoded-po33/frame.bin checked-po33/frame.bin
```

Exports include the frame, packet list, settings, patterns, sound headers and
sample WAVs. The sample WAVs contain entire stored pools; trim and slice
references are preserved in the raw records, not applied to the exported audio.

To replace an existing sample pool with a mono WAV:

```sh
./build/core/po33_encode_capture decoded-po33/frame.bin edited-po33.wav \
  --sample 0 replacement.wav
```

Replacement audio can use 8–192 kHz sample rates and different lengths. Shorter
audio is padded; longer audio grows the allocation. Existing trim coordinates,
slice references, patterns and other metadata are preserved, so appended audio
may lie outside the retained playback range. The tool cannot create a missing
pool or infer new trim settings. Larger allocations remain unverified on hardware.

Raw pattern edits are also supported:

```sh
./build/core/po33_encode_capture decoded-po33/frame.bin edited-pattern.wav \
  --pattern 0 edited-pattern-00.bin
```

These are structural record edits, not a complete musical-note editor. See
[PO-33/133 editing](core/examples/README.md#po-33--po-133-template-editing) for
byte edits, shared pools and conversion details.

### PO-35 / PO-137 speech

Extract speech records and playable previews, then replace an existing slot:

```sh
./build/core/po35_decode_capture backup.wav decoded-po35
./build/core/po35_encode_capture decoded-po35/frame.bin edited-po35.wav \
  --speech 0 spoken-intro.wav --fps 80
./build/core/po35_decode_capture edited-po35.wav checked-po35
```

Slots `0..14` must already exist in the template. Replacement speech can use
8–192 kHz WAV input. The default 80 fps permits up to eight seconds per
replacement; the tool also supports 20, 40 and 60 fps. Unselected speech, drum
patches, patterns, settings and opaque sample metadata are retained.

Previews are approximate and normalized per slot. They do not reproduce every
device voice or effect. See [speech replacement](core/examples/README.md#po-35--po-137-speech-replacement)
for duration limits, normalization and model details.

For either family's replacement audio, stereo WAVs require `--channel left`,
`--channel right` or `--channel mix`. Without edit options, the encoders rebuild
the original frame unchanged. `--rate 44100|48000|96000` selects the transfer
WAV rate, defaulting to 48000; it does not select the sample or speech rate.

### PO-32 patches and patterns

```sh
./build/core/po32_example
./build/core/po32_pattern_editor
./build/core/po32_decode_capture backup.wav decoded-po32
```

The example demonstrates a complete codec/modem loopback. The interactive
pattern editor supports four trigger lanes, accents and transfer-WAV export.
PO-32 transfers carry patch, pattern and state data for the device's own synth;
`po32_synth` renders drum audio locally. See the [example guide](core/examples/README.md).

## Use the C library

Install the built archive, public headers, CMake packages and pkg-config files:

```sh
cmake --install build --prefix "$PWD/install"
```

In a consuming CMake project:

```cmake
find_package(LibPO CONFIG REQUIRED)
target_link_libraries(app PRIVATE LibPO::po)
```

Pass the installation prefix in `CMAKE_PREFIX_PATH` when configuring that
project. For pkg-config consumers, put the installed pkg-config directory on
`PKG_CONFIG_PATH` if it is outside the system search path:

```sh
cc app.c $(pkg-config --cflags --libs po) -o app
```

Include `<po.h>` for all families or select individual headers:

| Headers | APIs |
| --- | --- |
| [`po32.h`](core/include/po32.h), [`po32_synth.h`](core/include/po32_synth.h) | Drum patches, patterns, framing, mono modem and drum synthesis |
| [`po33.h`](core/include/po33.h) | PO-33/133 framing, records and PCM sample conversion |
| [`po35.h`](core/include/po35.h), [`po35_analysis.h`](core/include/po35_analysis.h), [`po35_synth.h`](core/include/po35_synth.h) | PO-35/137 framing, records, PCM analysis and speech synthesis |
| [`po_stereo.h`](core/include/po_stereo.h) | Shared stereo transmission and offline reception |
| [`po_common.h`](core/include/po_common.h) | Shared status and packet types |

The build target and archive retain the names `po32` and `libpo32.a` for
compatibility. `LibPO32::po32`, `pkg-config po32` and existing public PO-32 C
names remain available. New consumers can use `LibPO::po` and `pkg-config po`.

## Use from Go

One module provides all families. It requires Go 1.26.1 or newer, cgo and a C99
compiler, and bundles the C sources so consumers do not need a separate CMake
installation. The module retains its historical `libpo32` import path despite
the repository rename.

To use the current development branch from your Go project:

```sh
go get github.com/ericlewis/libpo32/bindings/go@main
```

| Package | APIs |
| --- | --- |
| `github.com/ericlewis/libpo32/bindings/go` | Shared packets and status errors; package name `libpo` |
| `github.com/ericlewis/libpo32/bindings/go/po32` | Drum patches, patterns, synthesis and mono modem |
| `github.com/ericlewis/libpo32/bindings/go/po33` | PO-33/133 samples, patterns and framing |
| `github.com/ericlewis/libpo32/bindings/go/po35` | PO-35/137 speech, patterns and framing |
| `github.com/ericlewis/libpo32/bindings/go/stereo` | Shared stereo modem |

The former root PO-32 API moved to `/po32`. Call `Close` on objects that own
native resources. See the [Go binding guide](core/docs/BINDINGS.md) for examples,
ownership, migration and nested-module release tags.

## Documentation

| Guide | Contents |
| --- | --- |
| [C API](core/docs/C_API.md) | Functions, memory ownership and error contracts |
| [Architecture](core/docs/ARCHITECTURE.md) | Freestanding core, hosted adapters and shared code |
| [Backup formats](core/docs/PO_FORMATS.md) | Family layouts, raw fields and evidence limits |
| [PO-32 protocol](core/docs/PROTOCOL.md) | PO-32 packet and modem format |
| [Drum synthesis](core/docs/SYNTH.md) | PO-32 synthesis behavior |
| [Patch parameters](core/docs/PATCH_PARAMETERS.md) | The 21 drum-patch parameters |
| [Examples](core/examples/README.md) | CLI options, replacement audio and offline capture checks |
| [Go binding](core/docs/BINDINGS.md) | Model packages and integration |
| [Fuzzing](core/fuzz/README.md) | Fuzz harnesses and differential checks |

Canonical C sources and headers live in `core/src` and `core/include`. Hosted
tools are in `core/examples`, native tests in `core/tests`, and the Go module
in `bindings/go`. The Go C bundle is generated from the canonical core.

## Projects using libpo

| Project | What it does |
| --- | --- |
| [Libpo32 for Schwung](https://github.com/mestela/schwung-libpo32) | A 16-voice drum synthesizer for Schwung on Ableton Move, built on the PO-32 engine |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for verification commands, clang-format 18,
optional Git hooks and the Conventional Commits policy. Changes to canonical C
sources must also update the [generated Go bundle](core/docs/BINDINGS.md#maintaining-the-bundled-core).

## License

Copyright (c) 2026 Eric Lewis. Licensed under the MIT License. See [LICENSE](LICENSE).
