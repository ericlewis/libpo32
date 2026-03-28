# Releasing libpo32

Run commands from the repo root unless a step says otherwise.

## Scope

The initial public launch covers the core C library only:

- `include/po32.h`
- `include/po32_synth.h`
- `src/po32.c`
- `src/po32_patch_import.c`
- `src/po32_synth.c`
- `examples/po32_example.c`
- `examples/po32_demo.c`
- `examples/po32_pattern_editor.c`
- `examples/po32_decode_capture.c`

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
