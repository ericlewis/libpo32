# Releasing libpo32

Run commands from the repo root unless a step says otherwise.

## Scope

The initial public launch covers the core C library only:

| Path | Role |
| --- | --- |
| `include/po32.h` | Transfer builder, packet, render, and decode API |
| `include/po32_synth.h` | Drum synth API |
| `src/po32.c` | Core transfer implementation |
| `src/po32_patch_import.c` | `.mtdrum` patch text importer |
| `src/po32_synth.c` | Drum synth implementation |
| `examples/po32_example.c` | Minimal roundtrip example |
| `examples/po32_demo.c` | End-to-end transfer plus synth demo |
| `examples/po32_pattern_editor.c` | Interactive pattern editor and exporter |
| `examples/po32_decode_capture.c` | Transfer WAV decoder and packet dumper |

## Checklist

1. Set the release version in `VERSION`.
2. Update `CHANGELOG.md` with the release date and notes.
3. Verify the core release metadata is in sync:
   `./scripts/check-version-sync.sh`
4. Run the core verification suite:
   `./scripts/ci-verify.sh core`
5. Review release-facing docs and versions:
   `README.md`
   `docs/ARCHITECTURE.md`
   `docs/PROTOCOL.md`
   `docs/C_API.md`
   `docs/SYNTH.md`
   `docs/PATCH_PARAMETERS.md`
   `examples/README.md`
   `CHANGELOG.md`
   `VERSION`
   `CMakeLists.txt`
6. Check `git status` for unrelated workspace changes before tagging.
7. Tag the release as `libpo32-vX.Y.Z` and push the tag.

## Notes

- The supported surface is builder, parser, renderer, synth, and one decoding
  path exposed through the C API.
- CMake is the authoritative build and test entrypoint for the initial launch.
