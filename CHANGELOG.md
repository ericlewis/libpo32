# Changelog

All notable changes to `libpo32` will be documented in this file.

The format is based on Keep a Changelog, and the project follows Semantic Versioning.

## [Unreleased]

### Fixed
- Packets delivered by `po32_demodulator_push(...)` now carry the same
  `offset` as the ones `po32_frame_parse(...)` reports for the same frame.
  The streaming path measured from the first byte after the preamble, so
  every offset was 128 bytes short and pointed inside the preamble when used
  to index a reconstructed frame.
- A packet is now committed before its callback runs, so
  `po32_demodulator_packet_count(...)` counts the packet a callback stops on.
  Previously the count omitted it and the demodulator resumed mid-packet,
  desyncing on the next push.
- Long frames whose audio ends exactly at `po32_render_sample_count(...)`
  samples now decode completely. The demodulator decides each bit at the
  symbol boundary that closes its correlation window, which for the final
  bit lies just past the last rendered sample; `po32_decode_f32(...)` now
  resolves that pending symbol instead of returning `PO32_ERR_FRAME`.

### Added
- `po32_demodulator_stopped(...)` — report that a callback returning nonzero
  has stopped the stream. The stop is terminal: later pushes are no-ops.
- `po32_demodulator_flush(...)` — signal end-of-audio to the streaming
  demodulator so a final pending symbol is resolved without trailing
  silence.

## [0.2.1] - 2026-03-28

### Fixed
- `ARCHITECTURE.md` now lists `po32_patch_import.c` as a core module.
- `C_API.md` now documents `PO32_ERR_PARSE`, the streaming modulator API,
  and `po32_patch_parse_mtdrum_text(...)`.
- `README.md` now lists the `po32_example` command in the examples table.

### Changed
- Expanded test coverage across all `core/src/` and `core/examples/` files,
  covering error paths, edge cases, and previously untested branches in the
  import parser, synth renderer, frame builder, and example programs.

## [0.2.0] - 2026-03-28

### Fixed
- Pattern packet encode/decode now use the correct per-lane on-wire layout:
  `16` trigger bytes, then `16` morph pairs, repeated four times. This fixes
  patterns that decoded locally but landed on the wrong steps or instruments
  on real PO-32 hardware.
- Pattern trigger handling now follows the verified wire semantics: a zero
  low nibble means an empty step, and active triggers require a non-zero
  fill-rate nibble.

### Added
- `po32_pattern_step_t` struct with decoded `instrument`, `fill_rate`, and
  `accent` fields.
- `po32_pattern_packet_t.steps[]` array: populated during decode, consumed
  during encode. This replaces the raw `trigger_lanes[]` byte array as the
  primary interface for pattern data.
- High-level pattern builder helpers:
  `po32_pattern_init(...)`, `po32_pattern_clear(...)`,
  `po32_pattern_set_trigger(...)`, `po32_pattern_clear_trigger(...)`,
  `po32_pattern_clear_step(...)`, and `po32_pattern_set_accent(...)`.
- `po32_decode_capture` example: decodes a transfer WAV to per-packet dumps
  and a compact pattern summary.
- `po32_pattern_editor` starter presets for quick hardware-safe pattern
  export.

### Changed
- The library is now fully freestanding. All `memset`, `memcpy`, `memcmp`,
  `strlen`, and `strtof` calls have been replaced with self-contained
  implementations. Only the freestanding headers `<stddef.h>` and
  `<stdint.h>` are used. No libc runtime dependency.
- `po32_pattern_packet_t` no longer contains a `trigger_lanes[]` field.
  Use `steps[]` instead. The raw trigger lane bytes are derived during
  encode and consumed during decode internally.

## [0.1.0] - 2026-03-27

- Initial release of the core C library, public headers, and C examples.
- Release docs and verification scripts covering the core C library.
