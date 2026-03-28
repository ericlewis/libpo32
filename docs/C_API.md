# C API Guide

This document is the practical guide to the raw C surface in
[`../include/po32.h`](../include/po32.h)
and
[`../include/po32_synth.h`](../include/po32_synth.h).

## Core Responsibilities

The C surface is responsible for:

- protocol primitives
- frame building
- frame parsing
- typed packet encode/decode
- mono float rendering
- mono float decode
- synth rendering

It is not responsible for:

- WAV parsing
- device capture or playback
- OS audio sessions
- UI or file management

## Typical Transmit Flow

The normal transmit path is:

1. build packet payloads
2. append them to a `po32_builder_t`
3. finish the frame
4. render the frame to mono float samples

Minimal shape:

```c
po32_builder_t builder;
uint8_t frame[8192];
size_t frame_len = 0;

po32_builder_init(&builder, frame, sizeof(frame));
po32_builder_append_packet(&builder, tag_code, payload, payload_len, NULL);
po32_builder_finish(&builder, &frame_len);
```

Render:

```c
size_t sample_count = po32_render_sample_count(frame_len, 44100u);
float *samples = malloc(sample_count * sizeof(*samples));
po32_render_dpsk_f32(frame, frame_len, 44100u, samples, sample_count);
```

## Typical Receive Flow

Use [`po32_decode_f32(...)`](../include/po32.h)
when you already have the full mono sample buffer.

```c
po32_decode_result_t result;
uint8_t frame[65536];
size_t frame_len = 0;

po32_decode_f32(samples,
                sample_count,
                44100.0f,
                &result,
                frame,
                sizeof(frame),
                &frame_len);
```

`po32_decode_f32(...)` reconstructs a normalized frame from the recovered
packet stream.

## Parsing Frames

If you already have a frame, use [`po32_frame_parse(...)`](../include/po32.h).

The callback receives an owned [`po32_packet_t`](../include/po32.h),
including an owned payload buffer, so it is safe to keep after the callback
returns.

## Typed Packet Helpers

The core provides typed packet structs for the common packet families:

- patch
- knob
- reset
- state
- pattern

Use:

- `po32_packet_encode(...)`
- `po32_packet_decode(...)`

Use the typed packet structs plus `po32_packet_encode(...)` when constructing
wire payloads.

For higher-level pattern editing, prefer:

- `po32_pattern_init(...)`
- `po32_pattern_clear(...)`
- `po32_pattern_set_trigger(...)`
- `po32_pattern_clear_trigger(...)`
- `po32_pattern_clear_step(...)`
- `po32_pattern_set_accent(...)`

Those helpers keep `steps[]`, `morph_lanes[]`, and `accent_bits` in sync.

For pattern triggers, use:

- `po32_pattern_trigger_lane(...)`
- `po32_pattern_trigger_encode(...)`
- `po32_pattern_trigger_decode(...)`

These helpers handle the packed lane-byte format used by
`po32_pattern_packet_t.steps[]`.

Pattern packets are serialized per lane: `16` trigger bytes, then `16` morph
pairs, repeated four times, followed by reserved bytes and `accent_bits`.
A trigger is active only when its low nibble is non-zero. A low nibble of
`0x00` is empty on the wire.

## Body Bytes vs Frame Bytes

The C core works with full transmitted frames and encoded packet-body bytes.

Use:

- full `frame` bytes for rendering and full parsing
- the body slice `frame[PO32_PREAMBLE_BYTES .. frame_len - PO32_FINAL_TAIL_BYTES]`
  when you want just encoded packet-body bytes

That keeps packet-body inspection available without adding another public
abstraction.

## Error Handling

Most C functions return [`po32_status_t`](../include/po32.h):

- `PO32_OK`
- `PO32_ERR_INVALID_ARG`
- `PO32_ERR_RANGE`
- `PO32_ERR_BUFFER_TOO_SMALL`
- `PO32_ERR_FRAME`

Common causes:

- `INVALID_ARG`: null pointers, zero-length invalid combinations, bad sample rate
- `RANGE`: invalid payload shape or parameter count
- `BUFFER_TOO_SMALL`: caller-owned output buffer too small
- `FRAME`: malformed frame or decode failure

## Synth API

The synth is separate from transfer framing.

Use:

- `po32_synth_init(...)`
- `po32_synth_samples_for_duration(...)`
- `po32_synth_render(...)`

Deep synth parameter notes live in:

- [`./PATCH_PARAMETERS.md`](./PATCH_PARAMETERS.md)
