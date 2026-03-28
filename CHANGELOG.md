# Changelog

All notable changes to `libpo32` will be documented in this file.

The format is based on Keep a Changelog, and the project follows Semantic Versioning.

## [Unreleased]

## [0.2.0] - 2026-03-28

### Fixed
- Pattern trigger decode now correctly handles fill_rate=0. Previously, any
  trigger byte with a zero lower nibble was treated as empty, silently
  dropping instruments 5-16 when they appeared with fill=0 in real device
  backups. Only `0x00` is now treated as an empty step.
- Pattern trigger encode now accepts fill_rate=0 for instruments whose
  trigger byte is non-zero (instruments 5-16, or any instrument with accent).

### Added
- `po32_pattern_step_t` struct with decoded `instrument`, `fill_rate`, and
  `accent` fields.
- `po32_pattern_packet_t.steps[]` array: populated during decode, consumed
  during encode. This replaces the raw `trigger_lanes[]` byte array as the
  primary interface for pattern data.
- `po32_decode_capture` example: decodes a transfer WAV to per-packet dumps
  and a compact pattern summary.

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
