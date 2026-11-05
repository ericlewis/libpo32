# C API Guide

This guide covers the public C API. Include `po.h` for all modules, or include
only the model headers your application needs.

## Model-family APIs

Shared statuses and raw packets are in `po_common.h`. The `po_status_t`,
`po_packet_t` and `PO_*` names alias the existing PO-32 types and status values.

| Header | Role |
| --- | --- |
| [`po32.h`](../include/po32.h) | PO-32 packets, builders and mono modem |
| [`po32_synth.h`](../include/po32_synth.h) | Drum synthesis |
| `po33.h` | PO-33/133 framing, sample codec and raw-preserving logical records |
| `po35.h` | PO-35/137 framing, speech records, settings and patterns |
| `po35_analysis.h` | One-shot PCM-to-speech analysis with caller-owned workspace |
| `po35_synth.h` | Borrowed-record speech voices and one-shot rendering |
| `po_stereo.h` | Shared chunked stereo TX and caller-workspace offline RX |

All modules use caller-owned memory. Files, device access, normalization and
audio playback belong to the application. See [Architecture](ARCHITECTURE.md)
for build requirements and [Examples](../examples/README.md) for hosted use.

## PO-32 Core Responsibilities

| Included | Scope |
| --- | --- |
| Protocol primitives | Whitening, CRC/state updates, tag helpers |
| Frame building | `po32_builder_t` and full frame emission |
| Frame parsing | Packet walking over normalized frames |
| Typed packet encode/decode | Patch, knob, reset, state, and pattern packets |
| Mono float rendering | DPSK transfer audio output |
| Mono float decode | Transfer audio back to normalized frame bytes |
| Synth rendering | Local drum-hit rendering from patch parameters |

| Excluded | Scope |
| --- | --- |
| WAV parsing | File/container handling stays in examples or apps |
| Device capture or playback | No microphone, speaker, or audio-device control |
| OS audio sessions | No Core Audio, ALSA, WASAPI, etc. |
| UI or file management | No app framework, editor, or filesystem workflow |

## PO-32 Transmit Flow

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

## PO-32 Receive Flow

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

## PO-32 Frame Parsing

If you already have a frame, use [`po32_frame_parse(...)`](../include/po32.h).

The callback receives a pointer to a [`po32_packet_t`](../include/po32.h) with
an inline payload buffer. Copy the packet struct to retain it after the
callback returns; the callback pointer itself is borrowed.

Returning nonzero from the callback stops parsing with `PO32_OK` before
validating the remainder. Validated packets may be delivered before a later
error. `out_tail` is written only when a valid final tail is reached and is
unchanged on early stop or error. Bytes after that tail are ignored. A
successful status from an early-stopping callback does not establish that
the complete frame is valid.

## PO-32 Typed Packet Helpers

The core provides typed packet structs for the common packet families:

| Family | Struct |
| --- | --- |
| Patch | `po32_patch_packet_t` |
| Knob | `po32_knob_packet_t` |
| Reset | `po32_reset_packet_t` |
| State | `po32_state_packet_t` |
| Pattern | `po32_pattern_packet_t` |

Use:

- `po32_packet_encode(...)`
- `po32_packet_decode(...)`

Use the typed packet structs plus `po32_packet_encode(...)` when constructing
wire payloads.

Typed conversion is not a byte-preserving path for every valid backup.
`po32_state_packet_t` holds at most 16 chain entries, while the official Basic
Beats 1 pack contains a valid 22-entry chain; its typed decode returns
`PO32_ERR_FRAME`. Raw packet parsing preserves that state payload. The chain
is ordered and may repeat pattern numbers; the device maximum is not established.

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

Pattern payloads begin with a one-byte pattern number, followed by four lane
blocks: `16` trigger bytes, then `16` two-byte morph pairs. The payload ends
with `16` reserved bytes and the two-byte little-endian `accent_bits`.
A trigger is active only when its low nibble is non-zero. A low nibble of
`0x00` is inactive, but the other bits and its parameter pair can still be
nonzero. Typed re-encoding emits zero for inactive triggers: 39 of 57 official
patterns change in this way, normalizing 602 trigger bytes. This is a known
byte-preservation limit, not evidence of changed playback semantics. Keep raw
packet payloads when exact preservation is required.

The field named `reserved` contains independently varying per-step data in
official packs; it is not padding. Its exact control/effect meaning remains
unresolved. Typed roundtrips preserve it, the parameter pairs and `accent_bits`.
Do not regenerate the accent mask solely from active trigger accents. See
[Backup formats](PO_FORMATS.md) for the canonical field map and evidence limits.

## Patch Text Import

Use `po32_patch_parse_mtdrum_text(...)` to import patch parameters from
Microtonic `.mtdrum` text format. This is the text representation used by
Sonic Charge Microtonic's copy/paste and file export features.
The importer resolves `ModMode` and `NEnvMod` before converting dependent
values, regardless of field order. `ModRate`/`ModAmt` use the selected
modulation mode, and `NEnvAtk`/`NEnvDcy` use the selected noise-envelope mode.
Attack and pitch-modulation conversions share the renderer's maps; see the
[parameter reference](PATCH_PARAMETERS.md#mapping-functions) for units,
nonlinear curves, and zero-rate behavior.

## PO-32 Streaming Modulator

For streaming (chunk-based) rendering, the modulator API provides incremental
control:

- `po32_modulator_init(...)` — prepare for rendering
- `po32_modulator_render_f32(...)` — render the next chunk of samples
- `po32_modulator_reset(...)` — restart rendering from the beginning
- `po32_modulator_done(...)` — check if all samples have been rendered
- `po32_modulator_samples_remaining(...)` — query how many samples remain

## PO-32 Streaming Demodulator

For streaming (chunk-based) decoding, the demodulator API accepts audio in
arbitrary-sized chunks and invokes a callback on each decoded packet:

- `po32_demodulator_init(...)` — prepare for decoding at a sample rate
- `po32_demodulator_push(...)` — feed the next chunk of audio samples
- `po32_demodulator_flush(...)` — signal end-of-audio after the final chunk
- `po32_demodulator_done(...)` — check if the final tail has been received
- `po32_demodulator_stopped(...)` — check if a callback stopped the stream
- `po32_demodulator_packet_count(...)` — query how many packets were decoded
- `po32_demodulator_tail(...)` — access the decoded final tail
- `po32_demodulator_desync(...)` — reset sync state for error recovery

Each bit is decided at the symbol boundary that closes its correlation
window, which for the frame's final bit lies just past the last rendered
sample. Call `po32_demodulator_flush(...)` after the last chunk so a capture
that ends exactly at `po32_render_sample_count(...)` samples (no trailing
silence) still completes, then query `po32_demodulator_done(...)`. Flush returns
nothing and is a no-op on a NULL, already-done, or not-yet-synced demodulator.
`po32_demodulator_desync(...)` also returns nothing, but clears synchronization
and buffered packet data for any non-NULL state while retaining done/stop flags.

A flush resolves one pending symbol only when its estimated phase has reached
at least half a symbol. This avoids decisions from near-empty correlation
windows, but does not guarantee rejection of every truncated capture. Query
`po32_demodulator_done(...)` after flushing to check whether a valid final tail
was recovered.

`po32_decode_f32(...)` is a one-shot convenience wrapper around this API and
flushes internally.

### Packet offsets

The callback is the same `po32_packet_callback_t` used by
`po32_frame_parse(...)`, and `packet->offset` means the same thing on both
paths: the packet's byte offset within the full transmitted frame, counting
the 128-byte preamble. It matches the offset `po32_builder_append_packet(...)`
reports for the same packet, so it can be used to index a reconstructed frame.

### Stopping early

Returning nonzero from the callback stops the stream, and the stop is
terminal. The unconsumed remainder of the current chunk is discarded, and
later `po32_demodulator_push(...)` calls with non-NULL state and sample pointers
are no-ops returning `PO32_OK` —
`po32_demodulator_stopped(...)` reports this state. Stopping has to be
terminal because `push` reports neither how many samples it consumed nor
where in the chunk it stopped, so the discarded audio cannot be re-fed; a
demodulator that simply resumed would restart mid-frame and desync.

`po32_demodulator_packet_count(...)` counts packets since the most recent sync
acquisition and resets when sync is reacquired. A packet is committed before
its callback runs, including the packet that stops the stream.

To reuse a stopped demodulator, re-initialize it with
`po32_demodulator_init(...)`. `po32_demodulator_desync(...)` resets sync state
only; it no more clears the stop than it clears `po32_demodulator_done(...)`.

## PO-32 Streaming Synth Voice

For streaming (chunk-based) synthesis, the voice API pre-computes voice
constants at init time and persists DSP state across calls:

- `po32_synth_voice_init(...)` — snapshot parameters and prepare for rendering
- `po32_synth_voice_render_f32(...)` — render the next chunk of samples
- `po32_synth_voice_reset(...)` — restart rendering from the beginning
- `po32_synth_voice_done(...)` — check if all samples have been rendered
- `po32_synth_voice_samples_remaining(...)` — query how many samples remain

`po32_synth_render(...)` is a one-shot convenience wrapper that renders up to
the smaller of the requested sample count and `out_capacity`. Insufficient
capacity truncates the hit and still returns `PO32_OK`. Use
`po32_synth_samples_for_duration(...)` to size the complete output, or keep a
streaming voice to render successive chunks.

## PO-32 Body Bytes and Frame Bytes

The C core works with full transmitted frames and encoded packet-body bytes.

Use:

- full `frame` bytes for rendering and full parsing
- the body slice `frame[PO32_PREAMBLE_BYTES .. frame_len - PO32_FINAL_TAIL_BYTES]`
  when you want just encoded packet-body bytes

That keeps packet-body inspection available without adding another public
abstraction.

## PO-32 Error Handling

Most C functions return [`po32_status_t`](../include/po32.h):

- `PO32_OK`
- `PO32_ERR_INVALID_ARG`
- `PO32_ERR_RANGE`
- `PO32_ERR_BUFFER_TOO_SMALL`
- `PO32_ERR_FRAME`
- `PO32_ERR_PARSE`

Common causes:

- `INVALID_ARG`: null pointers, zero-length invalid combinations, bad sample rate
- `RANGE`: invalid payload shape or parameter count
- `BUFFER_TOO_SMALL`: caller-owned output buffer too small
- `FRAME`: malformed frame or decode failure
- `PARSE`: text import failed to parse a field value

## PO-32 Synth API

The synth is separate from transfer framing.

Use:

- `po32_synth_init(...)`
- `po32_synth_samples_for_duration(...)`
- `po32_synth_render(...)`

Deep synth parameter notes live in:

- [`./PATCH_PARAMETERS.md`](./PATCH_PARAMETERS.md)

## PO-33 and PO-35 Frames

`po33_frame_parse` and `po35_frame_parse` validate their model's counter
preamble, packet checksums and termination. Unknown packet tags remain raw.
Callbacks can receive a valid prefix before a later error. A successful status
alone does not establish completion: inspect `result.done` and `result.stopped`.
Trailing bytes are excluded from `result.frame_bytes`; compare that value with
the input length when exact consumption is required.

Both families provide `*_builder_init`, `*_builder_append_packet` and
`*_builder_finish`. Builders use caller-owned storage and reject capacity
errors before writing a partial packet. PO-33 emits its BEGIN/END packets
automatically; PO-35 emits its special final tail at finish. Do not append these
terminators as ordinary packets. Finish is idempotent.

## PO-33 Records and Samples

`po33_settings_*`, `po33_pattern_*` and `po33_sound_header_*` encode and decode
fixed-size logical records. A logical record can span several wire packets;
the application collects those payloads before calling the record codec.
Unknown fields, unused chain entries and raw sample ranges are preserved.
Input and output storage must not overlap.

Pattern event words remain raw. In the descriptive q0–q3 partition, q0 has an
observed pitch role and q1 an observed level role; q2 controls a filter-like
response, with cutoff a strong inference. q3's resonance interpretation remains
an unconfirmed candidate. Exact scales, field widths, zero/sentinel behavior
and playback state carry are unresolved. Preserve parameter-only rows with
zero control bytes. See [Backup formats](PO_FORMATS.md) for the field map and
the distinction between observed behavior and candidate meanings.

`po33_sample_decode` expands one signed-order mu-law byte to PCM16.
`po33_sample_encode` selects the nearest representable level, with ties toward
zero. The nominal sample clock is 23,437.5 Hz. Headers and full allocated pools
must be handled by the application; the sample codec does not apply trims or
resolve slice references. Resolve a source selector against the whole stored
pool, not its owner's trimmed interval. Raw range endpoints can overlap or
exceed an allocation; endpoint inclusion and the cause of those anomalous
ranges remain unresolved. Do not clamp, wrap or rescale them on decode.

`po33_sample_count_for_pcm(count, rate, &required)` sizes a bulk conversion from
finite normalized mono PCM at 8,000–192,000 Hz. Except for the native WAV alias
23,438 Hz, the output count is `ceil(count * 46875 / (2 * rate))`, targeting the
nominal 23,437.5 Hz clock. The alias keeps the input count unchanged.

`po33_samples_encode_f32(pcm, count, rate, output, capacity, &written)` performs
bandlimited sample-rate conversion and emits one mu-law byte per output sample.
For example, one second at 48 kHz needs 23,438 output bytes. Input samples must
be finite and in `[-1,1]`; invalid samples are rejected before any output writes.
Resampling overshoot is saturated before PCM quantization. No input gain
normalization is performed.

Both functions set their output count to zero on error. Empty input produces
zero samples; input and output buffers may be null when their required lengths
are zero. Buffers and count storage must not overlap. The caller supplies memory;
there is no heap allocation, libc or libm dependency. The bulk API creates only
sample bytes, leaving block padding, headers and template edits to the caller.

## PO-35 Records

`po35_settings_*`, `po35_pattern_*`, `po35_speech_header_*` and
`po35_speech_record_*` provide lossless structural codecs. The nine-byte speech
header contains a slot, record count, frame rate and five raw metadata bytes.
Each speech record is 44 bytes. Group collection, ordering and completeness
checks belong to the application, not the frame parser.

Settings contain a variable pattern chain, limited to `PO35_CHAIN_MAX_COUNT`
entries by the packet payload capacity. The record codecs preserve fields
without normalizing their values. PO-35 drum patches use `po32_patch_packet_t`
and `po32_packet_encode` / `po32_packet_decode`.

See [Backup formats](PO_FORMATS.md) for record offsets and unresolved fields.
The shared stereo transport does not imply shared PO-33/35 field semantics.
Speech header metadata, exact voice/effect behavior and gain laws remain
unresolved; structural preservation does not establish model identity or
physical cross-loading compatibility.

## Shared Stereo Modem

`po_stereo_modulator_init` borrows a nonempty wire buffer for chunked rendering.
The buffer must remain alive and immutable whenever the state may render or
reset, including subsequent renders after reset. It accepts
44,100, 48,000 and 96,000 Hz and produces interleaved left/right floats.
`po_stereo_modulator_render_f32` capacities and output counts are **stereo
frames**, so a capacity of 256 requires storage for 512 floats. Chunk boundaries
do not change the waveform. The modem does not validate packet grammar.

The stereo receiver is offline and requires the complete capture. Call
`po_stereo_decode_requirements` for workspace and wire-buffer capacities, then
provide those buffers to `po_stereo_decode_f32`. Workspace sizes are element
counts: `double` elements for scalars, bytes for scratch and recovered wire.
Input samples remain unchanged, and all buffers must be nonoverlapping.

The receiver accepts finite interleaved float input at 32,000 through
192,000 Hz. Channel order is explicit. It finds the counter preamble and
recovers wire bytes, potentially including bytes beyond the final packet.
Always pass those bytes to the relevant model parser to establish completion.

## PO-35 Speech Analysis

`po35_analysis.h` converts finite normalized mono `float` PCM in `[-1,1]`
at 8,000–192,000 Hz into packed 44-byte speech records. It is a one-shot,
approximate LPC analysis model, not the original Bitspeek or PO-35 encoder.
Both the 10-coefficient low bank and 30-coefficient high bank are emitted using
a common bandlimited 24 kHz clock, with the low bank decimated to 8 kHz.

Call `po35_analysis_size_for_pcm` before providing output storage. Supported
frame rates are 20, 40, 60 and 80 fps. The required record count is
`ceil(sample_count * fps / sample_rate)`, with a partial final record zero-padded.
The core allows at most `PO35_ANALYSIS_MAX_RECORDS` (65,535) records, matching
the header's 16-bit count. The hosted editor's smaller
[creation limit](../examples/README.md#po-35--po-137-speech-replacement) does not
restrict raw parsing or this API.

For example, encode one second of 24 kHz mono PCM at 80 fps:

```c
#include "po35_analysis.h"

po_status_t encode_second(po35_analysis_workspace_t *workspace,
                          const float pcm[24000],
                          uint8_t records[80 * PO35_ANALYSIS_RECORD_BYTES]) {
  size_t required = 0, written = 0;
  po_status_t status = po35_analysis_size_for_pcm(24000, 24000, 80, &required);
  if (status != PO_OK)
    return status;
  return po35_analysis_encode_f32(workspace, pcm, 24000, 24000, 80,
                                   records, required, &written);
}
```

The caller owns the workspace, input and output arrays. The workspace is
currently 49,632 bytes on supported IEEE-754 targets; use
`sizeof(po35_analysis_workspace_t)` and choose storage appropriate to the
application's stack budget. Its members are implementation details and need
no initialization. Workspace, PCM, records and output-length storage must be
disjoint. No allocation or persistent global state is used.

Invalid sample values, rate, fps or duration return `PO_ERR_RANGE`; insufficient
capacity returns `PO_ERR_BUFFER_TOO_SMALL`. Argument and sample validation
precedes record writes. On error, records remain untouched and the output byte
count is zero. Empty PCM requires zero record bytes, but still requires a
workspace; PCM and record pointers may be null when their required size is zero.

Analysis does not normalize input. Shared gain is calibrated to the louder
bank under reconstruction model 1; synthesized transients can still exceed
`[-1,1]`. Applications collect records into speech groups and update their
headers before using the frame builder and stereo transmitter.

## PO-35 Speech Rendering

`po35_synth.h` provides approximate speech reconstruction. Exact hardware voice
characters and effects are not implemented. Use `po35_analysis.h` for PCM input.
The low bank renders at 8 kHz; the high bank renders at 24 kHz. Supported record
rates are 20, 40, 60 and 80 fps.

```c
#include "po35_synth.h"

po_status_t render_speech(const uint8_t *records, size_t record_bytes,
                          uint32_t fps) {
  po35_synth_t synth;
  po35_synth_voice_t voice;
  float block[256];
  size_t written;
  po_status_t status = po35_synth_init(&synth, PO35_SYNTH_BANK_HIGH,
                                     PO35_SYNTH_DEFAULT_SEED);
  if (status == PO_OK)
    status = po35_synth_voice_init(&voice, &synth, records, record_bytes, fps);
  while (status == PO_OK && !po35_synth_voice_done(&voice)) {
    status = po35_synth_voice_render_f32(&voice, block, 256, &written);
    /* Consume written samples, including a finite prefix on numerical error. */
  }
  return status;
}
```

Records are borrowed and must remain immutable while the voice can use them,
including after reset. Record bytes must be a multiple of 44. Use
`po35_synth_samples_for_records` to calculate output length. Frame timing,
oscillator phase, noise and filter state persist across calls; chunked rendering
and reset reproduce the same output within a build.

Output is finite, unnormalized float and may exceed `[-1, 1]`. The renderer
performs no resampling, clipping or normalization. Numerical overflow or
instability returns a latched `PO_ERR_RANGE`; `written` counts only committed
finite samples.
Reset clears the error. `po35_synth_render` wraps the same voice implementation
for one-shot use and writes nothing when output capacity is insufficient.
