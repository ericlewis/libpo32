# Protocol Notes

This is a focused reference for the PO-32 transfer format implemented by
`libpo32`.

## Frame Layout

A transmitted frame is:

1. `128` bytes of preamble
2. encoded packet body bytes
3. `5` bytes of final tail

Constants live in [`../include/po32.h`](../include/po32.h).

## Preamble

The preamble is:

- `124` bytes of `0x55`
- sync word `14 19 9D CF`

Use [`po32_preamble_bytes()`](../include/po32.h) to
retrieve the canonical preamble bytes.

## Packet Body Format

Each encoded packet in the body is:

1. tag code, `2` bytes, little-endian
2. payload length, `1` byte
3. payload bytes
4. trailer, `2` bytes

The trailer stores the 16-bit protocol state immediately before trailer
emission.

## Stateful Whitening

The wire format is stateful:

- each raw byte is whitened by the low byte of the current state
- the state advances with `po32_crc_update(...)`
- trailer bytes are emitted using the post-packet state

Relevant helpers:

- [`po32_crc_mix_state(...)`](../include/po32.h)
- [`po32_crc_update(...)`](../include/po32.h)
- [`po32_whiten_byte(...)`](../include/po32.h)
- [`po32_unwhiten_byte(...)`](../include/po32.h)

Initial state:

- `0x1D0F`

## Final Tail

After the last packet, the builder emits a five-byte final tail:

1. marker `0xC3`
2. marker `0x71`
3. special byte derived from the current state
4. final state low byte
5. final state high byte

The tail is parsed into [`po32_final_tail_t`](../include/po32.h).

## Known Tags

| Tag | Code | Typical Payload |
| --- | ---: | --- |
| Patch | `0x37B2` | `43` bytes |
| Reset | `0x4AB4` | `1` byte |
| Pattern | `0xD022` | `211` bytes |
| Erase | `0xB892` | `1` byte |
| State | `0x505A` | `35 + pattern_count` bytes |
| Knob | `0xA354` | `2` bytes |

Use [`po32_tag_name(...)`](../include/po32.h) for a
human-readable tag name.

## Typed Packets

The core provides typed packet structs:

| Family | Struct |
| --- | --- |
| Patch | [`po32_patch_packet_t`](../include/po32.h) |
| Knob | [`po32_knob_packet_t`](../include/po32.h) |
| Reset | [`po32_reset_packet_t`](../include/po32.h) |
| State | [`po32_state_packet_t`](../include/po32.h) |
| Pattern | [`po32_pattern_packet_t`](../include/po32.h) |

And typed encode/decode helpers:

| Helper | Purpose |
| --- | --- |
| [`po32_packet_encode(...)`](../include/po32.h) | Typed packet struct to payload bytes |
| [`po32_packet_decode(...)`](../include/po32.h) | Payload bytes to typed packet struct |

For `po32_pattern_packet_t`, `steps[]` stores decoded per-lane step entries,
not raw lane bytes.

For normal pattern construction, prefer the `po32_pattern_*` helpers in
[`../include/po32.h`](../include/po32.h) rather than writing `steps[]`,
`morph_lanes[]`, and `accent_bits` manually.

On the wire, each pattern payload is lane-chunked:

1. `16` trigger bytes for lane 0
2. `16` morph pairs for lane 0
3. `16` trigger bytes for lane 1
4. `16` morph pairs for lane 1
5. `16` trigger bytes for lane 2
6. `16` morph pairs for lane 2
7. `16` trigger bytes for lane 3
8. `16` morph pairs for lane 3
9. `16` reserved zero bytes
10. `2` bytes accent bitmask, little-endian

Each trigger byte is packed. The lane index, selector bits, accent bit, and
low nibble together determine the triggered sound and step behavior. A zero low
nibble means the step is empty.

The lane grouping is:

- lane 0: `1, 5, 9, 13`
- lane 1: `2, 6, 10, 14`
- lane 2: `3, 7, 11, 15`
- lane 3: `4, 8, 12, 16`

Use:

- `po32_pattern_init(...)`
- `po32_pattern_clear(...)`
- `po32_pattern_set_trigger(...)`
- `po32_pattern_clear_trigger(...)`
- `po32_pattern_clear_step(...)`
- `po32_pattern_set_accent(...)`
- `po32_pattern_trigger_lane(...)`
- `po32_pattern_trigger_encode(...)`
- `po32_pattern_trigger_decode(...)`

## Body vs Frame

The project uses two byte slices repeatedly:

- `body_bytes`
- `frame_bytes`

`body_bytes` means the encoded packet body only. It excludes:

- the preamble
- the final tail

`frame_bytes` means the full transmitted frame. The renderer expects full frame
bytes, not body bytes.

## Patch Parameter Reference

The deep patch and synth parameter notes live separately in:

- [`./PATCH_PARAMETERS.md`](./PATCH_PARAMETERS.md)
