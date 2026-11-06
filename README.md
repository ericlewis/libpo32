# libpo

[![Tests](https://github.com/ericlewis/libpo/actions/workflows/tests.yml/badge.svg?branch=main&event=push)](https://github.com/ericlewis/libpo/actions/workflows/tests.yml)

A C library and command-line tools for decoding, editing and generating
Teenage Engineering Pocket Operator backups.

- **PO-32** — drum patches, patterns and synthesis
- **PO-33 / PO-133** — samples and patterns
- **PO-35 / PO-137** — speech, drum patches and patterns

The C99 core uses caller-owned memory and has no heap or OS dependencies.
The command-line tools read and write WAV files. PO-35 speech reconstruction
is approximate; see [format and hardware validation limits](core/docs/PO_FORMATS.md#limits).

## Build and try

Requires CMake 3.16 or newer and a C99 compiler. From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
(cd build && ./core/po32_demo)
```

The demo round-trips a PO-32 transfer and renders a drum hit.

## Documentation

- [Command-line guide](core/examples/README.md) — decode backups and replace samples or speech
- [C API guide](core/docs/C_API.md) — install, link and use the library
- [Documentation index](core/docs/README.md) — architecture, formats and technical references

## Projects using libpo

[Libpo32 for Schwung](https://github.com/mestela/schwung-libpo32) is a drum
synthesizer for Ableton Move.

[Contributing](CONTRIBUTING.md) · [MIT license](LICENSE)
