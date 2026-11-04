# Releasing libpo

Run commands from the repo root unless a step says otherwise.

## Scope

The release covers the freestanding C library and its model-specific Go
packages. The repository, legacy package names and library filename still use
`libpo32`; the current umbrella CMake package is `LibPO`.

| Path | Role |
| --- | --- |
| `core/include/` | Public PO-32, PO-33/133, PO-35/137 and shared transport APIs |
| `core/src/` | Freestanding codecs, modems, sample conversion and sound engines |
| `core/examples/` | Hosted WAV tools, template editors and PO-32 demos |
| `core/tests/` | Family codecs, audio processing, preservation and package checks |
| `core/fuzz/` | Existing PO-32 libFuzzer harnesses |
| `bindings/go/` | One Go module with PO-32, PO-33/133, PO-35/137 and stereo packages |

## Checklist

1. Set the release version in `VERSION`.
2. Update `CHANGELOG.md` with the release date and notes.
3. Verify the core release metadata is in sync:
   `./scripts/check-version-sync.sh`
4. Run the core verification suite:
   `./scripts/ci-verify.sh core`
5. Refresh and verify the bundled C sources with `./scripts/sync-go-core.sh`
   and `./scripts/sync-go-core.sh --check`. Run Go checks:
   `(cd bindings/go && GOEXPERIMENT=cgocheck2 go test -race ./...)`
   and `./scripts/check-go-module.sh`.
6. Review release-facing docs and versions:
   `README.md`
   `core/docs/ARCHITECTURE.md`
   `core/docs/PROTOCOL.md`
   `core/docs/PO_FORMATS.md`
   `core/docs/C_API.md`
   `core/docs/SYNTH.md`
   `core/docs/PATCH_PARAMETERS.md`
   `core/docs/BINDINGS.md`
   `core/examples/README.md`
   `CHANGELOG.md`
   `VERSION`
   `CMakeLists.txt`
7. Check `git status` for unrelated workspace changes before tagging.
8. Tag the release and push the tag.

## Notes

- Consult `core/docs/C_API.md` for the native API surface and
  `core/docs/BINDINGS.md` for Go package coverage and migration notes.
- Keep software validation separate from physical-device acceptance in release
  notes. The stereo-family transfers and approximate speech model are not
  hardware-validated.
- CMake is the authoritative build and test entrypoint for the core C library.
- Go bindings use cgo and compile the C sources directly; no cmake step needed.
