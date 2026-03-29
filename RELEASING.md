# Releasing libpo32

Run commands from the repo root unless a step says otherwise.

## Scope

The release covers the core C library and language bindings:

| Path | Role |
| --- | --- |
| `core/include/po32.h` | Transfer builder, packet, render, and decode API |
| `core/include/po32_synth.h` | Drum synth API |
| `core/src/po32.c` | Core transfer implementation |
| `core/src/po32_patch_import.c` | `.mtdrum` patch text importer |
| `core/src/po32_synth.c` | Drum synth implementation |
| `core/examples/po32_example.c` | Minimal roundtrip example |
| `core/examples/po32_demo.c` | End-to-end transfer plus synth demo |
| `core/examples/po32_pattern_editor.c` | Interactive pattern editor and exporter |
| `core/examples/po32_decode_capture.c` | Transfer WAV decoder and packet dumper |
| `bindings/go/` | Go language bindings |

## Checklist

1. Set the release version in `VERSION`.
2. Update `CHANGELOG.md` with the release date and notes.
3. Verify the core release metadata is in sync:
   `./scripts/check-version-sync.sh`
4. Run the core verification suite:
   `./scripts/ci-verify.sh core`
5. Run the Go binding tests:
   `cd bindings/go && go test -race ./...`
6. Review release-facing docs and versions:
   `README.md`
   `core/docs/ARCHITECTURE.md`
   `core/docs/PROTOCOL.md`
   `core/docs/C_API.md`
   `core/docs/SYNTH.md`
   `core/docs/PATCH_PARAMETERS.md`
   `core/examples/README.md`
   `CHANGELOG.md`
   `VERSION`
   `CMakeLists.txt`
7. Check `git status` for unrelated workspace changes before tagging.
8. Tag the release and push the tag.

## Notes

- The supported surface is builder, parser, renderer, synth, and one decoding
  path exposed through the C API, plus Go bindings wrapping the full public API.
- CMake is the authoritative build and test entrypoint for the core C library.
- Go bindings use cgo and compile the C sources directly; no cmake step needed.
