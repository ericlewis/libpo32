# Releasing libpo

Releases contain the freestanding C library, public headers and hosted tools
for PO-32, PO-33/133 and PO-35/137. Run commands from the repository root.

## Checklist

1. Set the version in `VERSION` and update `CHANGELOG.md` with the release notes.
2. Run the native checks:

   ```sh
   ./scripts/check-version-sync.sh
   ./scripts/ci-format.sh
   ./scripts/ci-verify.sh core
   ./scripts/ci-verify.sh sanitize
   ./scripts/ci-static-analysis.sh
   ```

3. Review the README, [C API](core/docs/C_API.md),
   [format reference](core/docs/PO_FORMATS.md) and
   [example guide](core/examples/README.md). Keep software checks and
   physical-device acceptance distinct in release notes.
4. Check the working tree for unrelated changes, then create and push the
   repository release tag using the exact value in `VERSION`, such as `2.0.0`.

## Package compatibility

New consumers use `LibPO::po` or `pkg-config po`. The archive `libpo32.a`,
`LibPO32::po32` and `pkg-config po32` remain available for compatibility.
CMake is the authoritative build, test and installation entrypoint.
