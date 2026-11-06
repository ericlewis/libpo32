# Examples

These hosted tools read and write WAV files around libpo's freestanding C core.
Recording and playback are handled by your audio software. Run commands from
the repository root.

| Device | Workflow |
| --- | --- |
| PO-32 | [Build and run](#build), or [decode a capture](#capture-input) |
| PO-33 / PO-133 | [Decode, rebuild and edit sampler backups](../docs/guides/PO33.md) |
| PO-35 / PO-137 | [Decode, preview and replace speech](../docs/guides/PO35.md) |

Use the [C API guide](../docs/C_API.md) for application integration and the
[format reference](../docs/PO_FORMATS.md) for field meanings and evidence.

## Programs

| Example | Purpose |
| --- | --- |
| [`po32_example.c`](po32_example.c) | Minimal roundtrip: build a frame, render it to float audio, decode it back, and print packet summaries |
| [`po32_demo.c`](po32_demo.c) | Full demo: roundtrip plus drum synthesis. Writes `demo_modem.wav` and `demo_kick.wav` |
| [`po32_pattern_editor.c`](po32_pattern_editor.c) | Interactive terminal pattern editor for one PO-32 pattern, with four trigger lanes per step, per-step accents, starter presets, and transfer export |
| [`po32_decode_capture.c`](po32_decode_capture.c) | Decode a transfer WAV back to normalized frame bytes, per-packet payload dumps, and a compact `pattern_summary.txt` |
| [`po33_decode_capture.c`](po33_decode_capture.c) | Decode PO-33/133 records and export sample WAVs |
| [`po33_encode_capture.c`](po33_encode_capture.c) | Re-encode a PO-33/133 frame, with optional sample and pattern edits |
| [`po35_decode_capture.c`](po35_decode_capture.c) | Decode PO-35/137 records and render approximate speech previews |
| [`po35_encode_capture.c`](po35_encode_capture.c) | Replace speech from PCM WAVs in a PO-35/137 frame and emit stereo modem audio |
| [`po_inspect.c`](po_inspect.c) | Print a stable structural report for one complete PO-33 or PO-35 raw frame |
| [`po_diff.c`](po_diff.c) | Compare two complete raw frames by bytes, packet topology, and raw-record topology |
| [`po_capture_doctor.c`](po_capture_doctor.c) | Diagnose one stereo capture across both channel orders and both backup parsers without exporting a frame |
| [`po_transfer_verify.c`](po_transfer_verify.c) | Compare one explicit raw frame with both recovery orders from one stereo capture |
| [`po35_transfer_manifest.c`](po35_transfer_manifest.c) | Describe observed PO-35 partial-transfer pattern/chain/speech relationships without projecting a transfer |

## Build

From the repo root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/core/po32_example
./build/core/po32_demo
./build/core/po32_pattern_editor
./build/core/po32_decode_capture input.wav decoded-po32
./build/core/po_inspect --family po33 decoded-po33/frame.bin
./build/core/po_diff --family po33 before/frame.bin after/frame.bin
./build/core/po_capture_doctor input.wav
./build/core/po_capture_doctor --json input.wav
./build/core/po_transfer_verify --family po33 decoded-po33/frame.bin readback.wav
./build/core/po35_transfer_manifest decoded-po35/frame.bin
```

## Raw Frame Inspection

`po_inspect` and `po_diff` take raw `frame.bin` files emitted by the family
decoders. Select the parser family explicitly with `--family po33` or
`--family po35`; the commands never infer it from the input.

Both inputs must be exactly one complete frame for that family. Malformed,
incomplete, wrong-family, and trailing-byte inputs are rejected. These tools
do not scan captures, combine transfers, repair bytes, or reinterpret unknown
payloads. `po_inspect` emits the stable one-line frame report. `po_diff` emits
deterministic byte, packet, and record topology summaries; unrecognized tags
and ungroupable records are labelled `raw` with their tag and byte counts.

## Capture Input

The PO-32 decoder accepts PCM16/24/32 and IEEE float32 WAV, including extensible
WAV, and averages its input channels to mono. Select the relevant input channel
before decoding a multichannel recording if averaging would cancel or obscure
the modem signal.

### Stereo Capture Input

The PO-33 and PO-35 decoders accept stereo WAV files containing PCM16, PCM24,
PCM32 or IEEE float32 audio at 32–192 kHz. They load the complete capture into
memory. Preserve the two data channels; mixing them to mono loses information.

Choose a new output directory for each decode and a new output filename for
each encode; the tools will not overwrite existing outputs. The decoder checks
for a complete frame before creating exports. Keep `frame.bin`: it is the
decoded backup used as input to the encoder. The other raw exports include
`payloads.bin`, `packets.tsv` and `summary.txt`, alongside model-specific
records and audio. A failed decode is not a valid partial backup.

### Capture Diagnosis

`po_capture_doctor` reads one valid stereo capture and reports its source WAV
sample format, sample rate and frame count. It then runs the existing one-shot
receiver once with normal channel order and once with swapped order. For every
recovered wire span it reports both the PO-33 and PO-35 parser outcomes,
including completion; when the receiver returns no wire span, the family
parsers are explicitly `not-run`. Text is the default; `--json` emits the same
ordered candidate results as JSON.

The command writes no decoded frame and always reports `selection=none`. It
does not scan or stitch transfers, repair bytes, infer a model family, or
choose between candidates. `transport=not-recovered` means only that the
hosted receiver did not return a wire span; it does not assign a waveform
failure cause. A readable capture produces a diagnostic even if no candidate
completes; an unreadable or unsupported WAV returns status 1 after its short
diagnostic.

### Transfer Readback Verification

`po_transfer_verify` compares one explicit, complete raw reference frame with
one readable stereo capture. Select `--family po33` or `--family po35`; the
command runs the existing one-shot receiver for normal and swapped channel
order and reports both candidates. It does not repair bytes, stitch transfers,
infer a family, or choose a channel order.

```sh
./build/core/po_transfer_verify --family po33 decoded-po33/frame.bin readback.wav
```

Exit status 0 means at least one candidate parsed to a complete frame whose
consumed bytes exactly match the reference. The report may show raw receiver
bytes after that frame tail; those are reported as trailing bytes and are not
part of the compared frame. A readable mismatch returns status 1 after its
report. This is software recovery evidence only: it does not prove that a
physical device accepted a transfer or that an observed capture came from a
particular device state.

### PO-35 Transfer Shape Manifest

`po35_transfer_manifest` reads one complete raw PO-35/137 frame and reports
the observed sets of included pattern indices, active-chain indices, speech
headers, and active full-pattern speech events. It labels the two corpus-backed
set comparisons as `match`, `mismatch`, or `not-evaluable` and always ends with
`projection=none`. A malformed, incomplete, duplicated, or extended speech
group makes the speech comparison `not-evaluable`.

```sh
./build/core/po35_transfer_manifest decoded-po35/frame.bin
```

An `observed-partial-candidate` label means only that a frame with fewer than
16 patterns has the retained corpus relationship. `full-bank-like` means all
16 distinct pattern indices are present. Neither result predicts receiver
merge/clear behavior, creates a selective backup, or establishes a device
operation.

## Offline Capture Checks

The CMake scripts use locally supplied inputs and the built C examples. They
do not download captures or communicate with a device. Output directories must
be new; generated files are retained for inspection, including after failure.

To verify the pinned [official PO-33 recording](https://assets.teenage.engineering/_img/63580398d7d4c7df33b6af4a_original.wav),
keep it outside the source tree and provide its local path:

```sh
cmake -DDECODER="$PWD/build/core/po33_decode_capture" \
  -DINPUT_WAV="/path/to/official-po33.wav" \
  -DOUTPUT_DIR="$PWD/build/verified-po33" \
  -P core/tests/cmake/verify_po33_official.cmake
```

This checks the input hash, decoded frame and payload hashes, completion and
record counts. The output directory's parent must already exist.

To verify an unchanged frame through the encoder and decoder:

```sh
cmake -DENCODER="$PWD/build/core/po33_encode_capture" \
  -DDECODER="$PWD/build/core/po33_decode_capture" \
  -DINPUT_FRAME="$PWD/decoded-po33/frame.bin" \
  -DWORK_DIR="$PWD/build/roundtrip-po33" -DRATE=48000 \
  -P core/tests/cmake/verify_capture_roundtrip.cmake
```

For PO-35/137, select the `po35_encode_capture` and `po35_decode_capture`
executables and a decoded PO-35/137 frame. `RATE` accepts 44100, 48000 or 96000
and defaults to 48000. Success requires the decoded frame to match every input
byte; it does not establish device acceptance.

## Core and Host Responsibilities

The shared `po_capture` and `po_stereo_wav` adapters handle file I/O and
allocation. Modem, speech analysis and synthesis code live in the freestanding
C core. See [Architecture](../docs/ARCHITECTURE.md), the [C API](../docs/C_API.md),
and [Backup formats](../docs/PO_FORMATS.md).

The unchanged-frame roundtrips above verify byte preservation. Separate
physical evidence covers specific PO-33 transfers and parameter comparisons;
it does not establish acceptance of every edit or validate PO-133, PO-35/137
or speech-preview fidelity. See [Backup formats](../docs/PO_FORMATS.md) for the
tested scope and remaining uncertainties.
