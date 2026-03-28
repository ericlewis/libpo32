# Architecture

`libpo32` is intentionally small. The project is built around one core idea:

| Layer | Responsibility |
| --- | --- |
| C core | Owns the PO-32 protocol and DSP math |
| Application layers | Own files, UI, devices, and OS integration |

That split keeps the tree manageable.

## Freestanding

The library is freestanding C99. The only standard headers it uses are
`<stddef.h>` and `<stdint.h>`, which are guaranteed available in
freestanding implementations (C99 section 4 paragraph 6). All memory operations
(`memset`, `memcpy`, `memcmp`), string functions (`strlen`), float parsing
(`strtof`), and math functions (`sinf`, `cosf`, `powf`, etc.) are replaced
with self-contained implementations. There is no libc runtime dependency.

At optimisation levels like `-O2`, the compiler may recognise byte-loop
patterns and emit platform-optimised intrinsics (e.g. replacing a zero-fill
loop with `bzero`). This is standard compiler behaviour and produces faster
code. To prevent it on truly bare-metal targets, compile with `-fno-builtin`.

## Core Modules

The core is two translation units:

| File | Purpose |
| --- | --- |
| [`../src/po32.c`](../src/po32.c) | Transfer framing, packet encode/decode, render, and decode |
| [`../src/po32_synth.c`](../src/po32_synth.c) | Drum synth implementation |

Public headers:

| Header | Purpose |
| --- | --- |
| [`../include/po32.h`](../include/po32.h) | Transfer API |
| [`../include/po32_synth.h`](../include/po32_synth.h) | Drum synth API |

`po32.c` contains four related layers:

1. protocol primitives
2. frame parsing and frame building
3. typed packet encode/decode helpers
4. render and decode DSP

`po32_synth.c` is separate because it is orthogonal to transfer framing and has
its own internal math model.

## Data Model

There are three important byte domains:

1. typed packet structs
2. encoded packet body bytes
3. full transmitted frame bytes

The encoded packet body is the packet stream between the preamble and final
tail. It is protocol-level data: tag, payload length, payload, trailer.

The full transmitted frame is:

1. preamble
2. encoded packet body
3. final tail

It is useful to preserve both:

- `frame_bytes`
- `body_bytes`

That is deliberate. The body is useful for packet-level inspection, while the
full frame is what the renderer and parser operate on.

## Transmit Path

The normal transmit flow is:

1. build typed packet payloads
2. append them to a `po32_builder_t`
3. finish the builder to emit the final tail
4. render the resulting frame to mono `float` audio

The builder is the frame-construction primitive in the release surface.

## Receive Path

There is one receive model in the release surface:

- call `po32_decode_f32(...)`

`po32_decode_f32(...)` demodulates mono float audio and rebuilds a normalized
frame from the recovered packets.

## Why The Output Is “Normalized”

The decoder does not try to preserve every transport artifact from the original
capture. It reconstructs a clean frame from the recovered packet stream.

That means:

- packet order is preserved
- payloads and trailers are rebuilt consistently
- the output is suitable for rendering, parsing, and roundtrip testing

It does not mean:

- a byte-for-byte forensic copy of the noisy capture

That distinction matters when comparing decoded output to a recorded transfer.

## Float-Only Signal Boundary

The signal boundary is float-only:

- render output is `float`
- decode input is `float`

The core does not parse WAV, AIFF, Core Audio buffers, or device APIs. That
work belongs in examples or applications.
