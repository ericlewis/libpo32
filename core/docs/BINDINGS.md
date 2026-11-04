# PO-32 Language Bindings

This document describes the PO-32 binding conventions used by
[`bindings/go`](../../bindings/go). That binding targets `po32.h` and
`po32_synth.h`; the PO-33, PO-35 and shared stereo APIs are not wrapped.

## Rules

1. **Wrap the public API only.** Use declarations from `core/include/po32.h`
   and `core/include/po32_synth.h`. Do not expose private helpers or state from
   `core/src/`.

2. **Compile the C sources directly.** The PO-32 binding compiles three C
   source files (`po32.c`, `po32_patch_import.c`, `po32_synth.c`) as part
   of its own build. The Go binding includes each source through a separate
   cgo wrapper. Binding users do not need a prebuilt archive or CMake step.

3. **Use separate translation units.** The internal headers `po32_lut.h`
   and `po32_synth_tables.h` define `static` data tables. Including
   multiple `.c` files in a single translation unit causes redefinition
   errors. Compile each `.c` file separately.

4. **Package idiomatically.** Go bindings use `go.mod`, Swift uses
   `Package.swift`, Rust uses `Cargo.toml`. Each binding follows its
   language's standard packaging conventions.

5. **No binding may modify the C core.** If a binding needs a C-level
   change, that change goes through the normal core review process and
   must benefit all bindings equally.

## Directory Layout

```
bindings/
  <lang>/
    <package metadata>     # go.mod, Package.swift, Cargo.toml, etc.
    <ffi/bridge files>     # cgo includes, C shim, sys crate, etc.
    <idiomatic wrapper>    # the user-facing API in the target language
    <tests>
```

The Go binding follows this as:

```
bindings/go/
  go.mod
  cgo_po32.go              # compiles core/src/po32.c
  cgo_patch_import.go      # compiles core/src/po32_patch_import.c
  cgo_synth.go             # compiles core/src/po32_synth.c
  trampoline.c             # C callback bridge (see below)
  convert.go               # C↔Go struct conversion helpers
  <api files>.go           # idiomatic Go surface
  po32_test.go             # tests
```

## What to Wrap

PO-32 bindings should expose the public API from both PO-32 headers.
Organize the wrapper into these areas:

| Area | C functions |
| --- | --- |
| Protocol primitives | `po32_preamble_bytes`, `po32_tag_name`, CRC, whiten/unwhiten |
| Frame building | `po32_builder_init`, `_append_packet`, `_append`, `_finish`, `_reset` |
| Frame parsing | `po32_frame_parse` (requires callback bridge) |
| Typed packet codec | `po32_packet_encode`, `po32_packet_decode` (type-safe wrappers per tag) |
| Raw patch codec | `po32_encode_patch`, `po32_decode_patch` |
| Patch import | `po32_patch_parse_mtdrum_text` |
| Pattern helpers | `po32_pattern_init`, `_set_trigger`, `_clear_trigger`, `_clear_step`, `_set_accent`, trigger lane/encode/decode |
| Param helpers | `po32_patch_params_zero`, `po32_morph_pairs_default` |
| DPSK render | `po32_render_sample_count`, `po32_render_dpsk_f32`, streaming modulator |
| Audio decode | `po32_decode_f32` |
| Streaming decode | `po32_demodulator_init`, `_push`, `_flush`, `_done`, `_stopped`, `_desync`, `_packet_count`, `_tail` (requires callback bridge) |
| Drum synth | `po32_synth_init`, `_samples_for_duration`, `_render` |
| Streaming synth | `po32_synth_voice_init`, `_render_f32`, `_reset`, `_done`, `_samples_remaining` |

## Type Mapping Conventions

| C type | Binding type |
| --- | --- |
| `po32_status_t` | Language error type (Go `error`, Rust `Result`, Swift `throws`) |
| `size_t` output params | Return values (not output pointers) |
| `int` booleans | Native `bool` |
| `void *pkt` in encode/decode | Type-safe per-tag functions (not a single generic dispatch) |
| `float *out` buffers | Owned slices/arrays returned to caller |
| `po32_packet_callback_t` | Native closure/lambda/function (see callback section) |

## Constants

C `#define` macros are not linkable symbols. Expose protocol constants through
the binding's native constant mechanism, using the C declarations where the
toolchain supports them. Test critical values to catch drift.

## The Callback Problem

`po32_frame_parse` takes a C function pointer. Most languages cannot pass
a closure directly as a C function pointer. The standard solution:

1. Write a small C trampoline function that matches the
   `po32_packet_callback_t` signature.
2. The trampoline receives a handle (integer or opaque pointer) via the
   `void *user` parameter.
3. The trampoline calls back into the binding language using that handle
   to look up the real closure.
4. The binding registers the closure before calling `po32_frame_parse`
   and unregisters it afterward.

The Go binding does this with `trampoline.c` + a Go-side callback
registry protected by a mutex. The same pattern applies to Rust
(`extern "C" fn` + closure box), Swift (`@convention(c)` + context
pointer).

## Memory Ownership

The C core never allocates. All buffers are caller-owned. Bindings must:

- Allocate output buffers before calling C render/decode functions.
- Use `po32_render_sample_count` to size audio buffers.
- Copy frame bytes out of any internal builder buffer before returning
  them to the caller.
- Keep frame data alive for the lifetime of a streaming modulator.

Types that own native buffers or state (builder buffers, modulator copies)
should implement the language's cleanup pattern: `Close()` in Go,
`Drop` in Rust, `deinit` in Swift.

## Testing

PO-32 bindings must include tests covering:

- Tag name lookup for all six tags
- Whiten/unwhiten roundtrip
- Encode/decode roundtrip for each packet type (patch, knob, reset, state, pattern)
- Raw patch encode/decode
- Pattern set/clear/accent
- Builder → frame parse with callback
- Full roundtrip: encode → build frame → render audio → decode audio → parse → verify
- Streaming modulator: render in chunks, verify total matches `RenderSampleCount`
- Synth render: verify non-silent output in `[-1, 1]`
- MtDrum text import
- Error paths: empty data, invalid arguments, buffer overflow

## CI

Add a job to `.github/workflows/tests.yml` that builds and tests the
binding on both `ubuntu-latest` and `macos-latest`. Use the language's
native test runner with race/thread-safety checking enabled where
available.

## Checklist for a New PO-32 Binding

- [ ] Create `bindings/<lang>/` with language-standard package metadata
- [ ] Compile all three C sources as separate translation units
- [ ] Include only public headers (`core/include/`)
- [ ] Wrap every PO-32 public function with idiomatic types and error handling
- [ ] Implement the callback trampoline for `po32_frame_parse`
- [ ] Re-declare all `#define` constants as native constants
- [ ] Write the full test suite listed above
- [ ] Add CI job to `tests.yml` for ubuntu + macOS
- [ ] Add the binding to `RELEASING.md` scope table
- [ ] Add the binding to the `README.md` project layout table
