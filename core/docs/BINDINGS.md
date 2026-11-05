# Go Binding

One module, `github.com/ericlewis/libpo32/bindings/go`, exposes libpo through
model-specific packages. The module retains its historical `libpo32` import
path despite the repository rename to `libpo`.

| Package | Scope |
| --- | --- |
| `libpo` (module root) | Shared status errors and owned raw packets |
| `po32` | Drum patches, patterns, mono modem, drum synthesis and streaming APIs |
| `po33` | PO-33/133 records, sample conversion, symbol packing and framing |
| `po35` | PO-35/137 records, speech analysis/synthesis and framing |
| `stereo` | Shared stereo transmission and offline reception |

There is one `go.mod`, not a module per device. PO-35 drum helpers reuse
`po32.PatchPacket`; drum rendering uses `po32.NewSynth`. WAV files, template
editing and device I/O remain application responsibilities.

## Build and Consume

Go 1.26.1 or newer, cgo and a C99 compiler are required. The module bundles its
C sources, headers and license; no parent checkout or CMake installation is
required by consumers. From this repository:

```sh
./scripts/sync-go-core.sh --check
(cd bindings/go && go test -race ./...)
./scripts/check-go-module.sh
```

Import a model package, for example:

```go
import "github.com/ericlewis/libpo32/bindings/go/po33"
```

Before release, a consumer can use a local `go.mod` replacement:

```text
require github.com/ericlewis/libpo32/bindings/go v0.0.0
replace github.com/ericlewis/libpo32/bindings/go => /path/to/libpo/bindings/go
```

## Migration

The former root `po32` API moved to the `/po32` import path. The root package
is now `libpo`. `po32.Packet` and `po32.Status` alias the shared root types.
Status error strings now begin with `libpo:` instead of `po32:`; compare error
values or use `errors.Is` rather than matching message text.

## Ownership and Completion

Returned records, packets and audio are Go-owned values. Callback packets can
be retained after return. Objects that retain native buffers copy their input
to C-owned memory. Call `Close` on builders, modulators and streaming voices or
receivers; repeated Close is safe, and finalizers are only a fallback. Use
constructor-returned pointers and follow each type's concurrency contract.

Callbacks return true to continue, false to stop. PO-33/35 permit nil callbacks
for validation and return partial `DecodeResult` values on errors. Check the
error and `Done`; `Stopped` does not mean complete. Stereo decoding returns
raw wire bytes potentially beyond a terminator: validate with the family
parser and trim at `FrameBytes`. Stereo capacities count frames, two floats
each. PO-35 rendering may return a finite audio prefix together with a
numerical error; output is approximate and unnormalized.

If a PO-32 streaming callback panics or calls `runtime.Goexit`, the demodulator
closes because the native call could not finish committing its state. The panic
still propagates; create a new demodulator after recovering.

Release tags for this nested module use the prefix `bindings/go/`, for example
`bindings/go/v0.4.0`. A repository-root tag such as `v0.4.0` alone does not
publish that version of the Go module.

See the [C API](C_API.md) and [formats](PO_FORMATS.md) for the underlying
contracts and limitations. Software success does not prove device acceptance.

## Maintaining the Bundled Core

`core/` remains authoritative. `bindings/go/internal/ccore/SOURCES.txt` lists
bundled inputs. Run `./scripts/sync-go-core.sh` after changing them; never edit
generated copies directly. Its `--check` mode rejects missing, changed,
symlinked or unlisted bundle files. Each package compiles disjoint C sources
as separate translation units; callback shim/export names must be unique.

The root integration test links every family together. CI checks source sync,
race detection and strict cgo pointer checks (`GOEXPERIMENT=cgocheck2`). The
standalone check copies the module outside the checkout, tests it there, and
builds a separate consumer.
