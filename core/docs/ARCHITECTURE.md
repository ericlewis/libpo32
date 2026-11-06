# Architecture

`libpo` is one freestanding C library with model-family modules. Application
layers own allocation, files, UI, devices and OS integration. Model-specific
APIs keep the substantially different sound representations explicit; shared
private code handles the common wire and numerical operations.

## From recording to records

A typical backup workflow crosses four boundaries:

1. The host records or reads modem audio and supplies PCM samples to libpo.
2. A family modem recovers wire bytes. PO-32 has a streaming receiver;
   PO-33/133 and PO-35/137 use the shared offline stereo receiver.
3. The family parser validates and delivers packets. A callback can receive
   valid packets before a later error, so check the parser's completion result.
   Logical records such as patterns and sample pools can span multiple packets.
4. The application interprets or edits supported data, rebuilds a frame and
   uses the modem transmitter to produce audio. The host saves or plays it.

The core does not load WAV files or communicate with a device. The hosted
[examples](../examples/README.md) provide file-based workflows; applications
provide their own recording, playback and storage. Retain raw records when an
edit does not have enough information to safely interpret their contents.

## Freestanding contract

The core uses C99 and standard freestanding headers. It performs no heap
allocation, filesystem or device access, and calls neither libc nor libm.
Callers provide state, audio buffers and any whole-capture workspace. Floating
point assumes IEEE-754 binary32 and binary64 where documented.

GCC/Clang CMake core builds use `-ffreestanding -fno-builtin
-fno-stack-protector` to prevent compiler-generated hosted runtime calls.
Other toolchains need equivalent settings. On Unix, noninstrumented builds
audit the actual archive with a relocatable link and undefined-symbol check.
Sanitizer, coverage and fuzzer builds intentionally use a host runtime and audit
a separate uninstrumented source compilation. Hosted examples/tests retain their
normal runtime and math libraries. Architecture-specific compiler-builtins
(such as ARM software floating-point arithmetic) remain a toolchain concern;
cross-compilation alone is not runtime validation.

## Modules

| Module | Responsibility |
| --- | --- |
| `po_common.h` | Shared status, raw packets and trailers; preserves old PO-32 tags/layouts and supplies neutral aliases |
| `po32.c`, `po32.h` | PO-32 typed packets, mono modem, builders and receivers |
| `po32_patch_import.c` | `.mtdrum` text parser |
| `po32_synth.c`, `po32_synth.h` | Host drum rendering from PO-32 patch parameters |
| `po33.c`, `po33_audio.c`, `po33_data.c`, `po33.h` | PO-33/133 framing, PCM conversion, sample companding and lossless logical records |
| `po35.c`, `po35.h` | PO-35/137 framing, speech records, settings and patterns |
| `po35_analysis.c`, `po35_analysis.h` | Experimental one-shot PCM analysis with fixed caller-owned workspace |
| `po35_synth.c`, `po35_synth.h` | Experimental speech reconstruction with caller-owned streaming voices |
| `po_stereo.c`, `po_stereo.h` | Shared stereo DQPSK transmitter and explicit offline receiver |
| `po.h` | Optional umbrella include |

`po_wire_internal.h` is the shared source for CRC, whitening, ordinary packets,
4 KiB counter preambles and the special final tail. Family parsers retain their
own grammar and validation. PO-32's tag whitelist remains strict; the newer
families preserve unknown packets where their grammar allows them. Shared
transport does not imply shared record layouts or musical field meanings.

`po_math_internal.h` supplies bounded private double-precision DSP helpers.
PO-35 uses a double LAR table and double filter state for its 30th-order filter.
PO-32 retains its own lookup-table math.

PO-35 drum patches reuse `po32_patch_packet_t`, its existing packet codec and
`po32_synth`; there is no duplicate drum engine. PO-133 and PO-137 use the
33/35 format families, not separate implementations. A compatible format does
not identify the physical device that produced a recording.

## Byte domains and ownership

Keep raw packet payloads, encoded packet bodies and complete frames distinct.
Typed settings/pattern/speech records are another domain: one logical record
can span multiple packets. The PO-33/35 record codecs preserve raw fields,
including unused chain entries and anomalous sample ranges. PO-32 typed
pattern conversion normalizes inactive trigger bytes to zero; the resulting
loss of stored selector/accent bits is a byte-preservation limit, not a proven
playback change. Its 16-entry typed state capacity also excludes a valid
official 22-entry chain. Retain raw packet payloads for byte-exact backups.

PO-33 zero-control parameter rows must survive conversion. Sample aliases refer
to the whole stored pool, not the owner's trimmed interval, and endpoint
inclusion remains unresolved. PO-32's field named `reserved` contains independent
per-step data. These fields remain raw because observed roles do not establish
complete musical semantics; see the [canonical field map](PO_FORMATS.md).

Raw callbacks borrow a packet struct with inline payload bytes; copying the
struct retains its contents. Offsets include the complete model preamble. A
callback stop is separate from successful completion, and validated prefixes
can precede a later CRC failure. Consult each parser's documented result semantics.

Builders use caller-owned storage and check whole writes before changing it.
Borrowed renderer input must remain alive and immutable whenever the state may
render or reset, including subsequent renders after reset. There is no hidden
ownership transfer or background work.

## Audio boundaries

Modem, synthesis and bulk PCM buffers use `float`; the scalar PO-33 sample codec
converts between signed-order mu-law bytes and PCM16. Stereo capacities/counts
are explicitly in stereo frames. The shared transmitter is chunked and retains
phase and integer timing across calls. The PO-32 receiver remains streaming.

The stereo receiver is deliberately **offline**. Forward/backward audio and
pilot filters need the complete capture. Call `po_stereo_decode_requirements`
for typed workspace sizes and provide those arrays to `po_stereo_decode_f32`.
It preserves the input, recovers wire bytes from the counter preamble onward,
and leaves model grammar and final-frame trimming to the family parser.

`po33_samples_encode_f32` converts finite mono PCM to the nominal 23,437.5 Hz
sample clock and quantizes it with the existing mu-law encoder. The caller owns
input and output buffers; internal scratch storage is fixed and requires no
allocation. `po_resample_internal.h` shares the bandlimited Lanczos-8 kernel
with speech analysis. The 23,438 Hz WAV rate is treated as an exact native alias
to preserve exported sample counts.

`po35_analysis_encode_f32` analyzes complete finite mono PCM in `[-1,1]` at
8–192 kHz. It resamples onto a bandlimited 24 kHz analysis clock and derives
both the 24 kHz high bank and decimated 8 kHz low bank. A fixed caller-owned
workspace holds scratch state; input duration only changes the required output
size. Analysis emits 44-byte records at 20, 40, 60 or 80 fps and zero-pads a
partial final record. It does not allocate, normalize input or create a frame.

`po35_synth_voice_*` streams one borrowed speech group with fixed-size state.
Frame boundaries use cumulative integer rounding; reset and arbitrary chunks
produce the same output. Core speech output is finite, unnormalized audio and
may exceed `[-1,1]`. Numerical failure is explicit and latched until reset.
Speech-preview normalization, conversion to WAV PCM, clip concatenation and
file writing stay hosted.
The speech model uses deterministic Gaussian noise with a caller-selected seed.

## Hosted adapters

`examples/po_capture.c` reads mono/stereo PCM or float WAV files without mixing
channels; its capture adapter allocates workspace and calls the core stereo
receiver. `examples/po_stereo_wav.c` validates family frames, wraps the core
transmitter in WAV/PCM output, and handles exclusive file creation. The PO-33
and PO-35 examples collect logical records and export their contents.

`po33_encode_capture` uses the PCM converter for sample replacements. It retains
the original allocation for shorter audio and clears the unused tail to silence;
longer audio adds whole 4,096-byte sample blocks. Existing nine-byte headers,
trim/slice references and unrelated packets remain unchanged. Pool growth does
not expand the audible trim or establish hardware memory capacity.

`po35_encode_capture` selects or explicitly mixes speech WAV channels, calls
the core analysis API, and replaces existing speech groups. Its 640-record
creation limit is a hosted policy, separate from the core's 16-bit record-count
limit. It changes only selected speech data and header count/frame rate;
opaque metadata, controls, patterns and unselected content are retained.

## Build and package names

Use `po.h` or individual model headers. `LibPO::po` and `pkg-config po` provide
umbrella discovery; `LibPO32::po32`, `pkg-config po32`, the `po32` build target
and legacy library filename (`libpo32.a` on Unix) remain available. Public PO-32
symbols, concrete layouts and numeric status values are preserved. The repository
is named `libpo`; documented compatibility identifiers retain `libpo32`.

## Validation boundaries

Builds, byte-preserving roundtrips and numerical checks do not establish device
acceptance. Existing physical evidence covers specific PO-33 transfers and
parameter comparisons; it does not validate every generated transfer, PO-133,
or the PO-35/137 family. Unmapped controls remain raw. Speech analysis and
reconstruction are approximate; neither reproduces the original Bitspeek or
PO-35 implementation. See [Backup formats](PO_FORMATS.md) for the tested scope.
