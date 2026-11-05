# Examples

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

## Build

From the repo root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/core/po32_example
./build/core/po32_demo
./build/core/po32_pattern_editor
./build/core/po32_decode_capture input.wav decoded-po32
```

## Capture Input

The PO-32 decoder accepts PCM16/24/32 and IEEE float32 WAV, including extensible
WAV, and averages its input channels to mono. Select the relevant input channel
before decoding a multichannel recording if averaging would cancel or obscure
the modem signal.

### Stereo Capture Input

The PO-33 and PO-35 decoders accept stereo WAV files containing PCM16, PCM24,
PCM32 or IEEE float32 audio at 32–192 kHz. They load the complete capture into
memory. Preserve the two data channels; mixing them to mono loses information.

Decoder output directories and encoder output files must not already exist.
The decoder validates frame completion before creating outputs. Raw exports
include `frame.bin`, `payloads.bin`, `packets.tsv` and `summary.txt`, followed by
model-specific records and audio. A failed decode is not a valid partial backup.

## PO-33 / PO-133 Decode and Re-encode

```sh
./build/core/po33_decode_capture backup.wav decoded-po33
./build/core/po33_encode_capture decoded-po33/frame.bin rebuilt-po33.wav
./build/core/po33_decode_capture rebuilt-po33.wav checked-po33
cmp decoded-po33/frame.bin checked-po33/frame.bin
```

PO-33 uses the input channels in their supplied order. In addition to the raw
frame and packet exports, it writes `settings.bin`, `pattern-NN.bin`,
`sound-NNN.bin` and WAVs for populated sample pools. Sound blobs retain their
nine-byte headers and padding. Sample WAVs contain expanded PCM16 at 23,438 Hz,
the nearest integer to the nominal 23,437.5 Hz clock. Trims and slice references
are not applied.

Both stereo encoders default to 48 kHz. Use `--rate 44100`, `--rate 48000` or
`--rate 96000` to select the transfer WAV rate. Re-encoding without edits
preserves the normalized frame bytes, not the original recorded waveform.

## PO-33 / PO-133 Template Editing

Replace an existing sample pool with a nonempty PCM16, PCM24, PCM32 or float32
WAV at 8–192 kHz, including extensible WAV. The core converts it to the nominal
23,437.5 Hz clock; exported 23,438 Hz WAVs are treated as native-rate input.
Samples must be finite and in `[-1,1]`. The sample pool can be shared by several
drum slices. Pattern replacements remain raw records of their original length.

```sh
./build/core/po33_encode_capture decoded-po33/frame.bin edited-po33.wav \
  --sample 0 edited-sound-000.wav \
  --pattern 0 edited-pattern-00.bin
```

Mono input needs no channel option. Stereo requires an explicit global channel
choice; `left` and `right` select one channel, and `mix` takes their arithmetic
mean. Mono accepts `left` or `mix` and rejects `right`.

```sh
./build/core/po33_encode_capture decoded-po33/frame.bin edited-po33.wav \
  --sample 0 new-stereo-recording.wav --channel mix
```

Shorter audio is padded with silence to the existing allocation. Longer audio
grows the pool in 4,096-byte blocks, with silence in the final partial block;
it is never truncated to fit the old length. Growth requires a block-aligned
original pool and rejects ambiguous extensions in the selected sound group.
The editor preserves trim coordinates and slice references exactly. It does
not extend trims to cover appended audio or reclaim storage after a shorter
replacement. Device storage capacity and restoration of larger pools remain
unverified.

Selectors are zero-based and match the exported filenames. To replace one raw
pattern byte instead of the whole pattern:

```sh
./build/core/po33_encode_capture decoded-po33/frame.bin edited-pattern.wav \
  --pattern-byte 0 768 252
```

This changes a raw byte; it is not a musical-note editing API. Options can be
repeated for different records, and conflicting edits are rejected. The editor
preserves settings, patterns not explicitly edited, all sample headers and
unselected content. Existing packet lengths and order are retained; growth adds
sample-data packets to the selected group. It cannot create a new pool from a
missing or metadata-only entry, change trim endpoints or remap slices. Edited
PCM is quantized to the nearest representable mu-law level. Unchanged native
PCM retains its original byte, including the alternate zero encoding. Input is
not gain-normalized; overshoot from sample-rate conversion is saturated before
quantization. Outputs must not exist, and all edits validate before creation.

## PO-35 / PO-137 Decode and Speech Preview

```sh
./build/core/po35_decode_capture backup.wav decoded-po35
./build/core/po35_encode_capture decoded-po35/frame.bin rebuilt-po35.wav
./build/core/po35_decode_capture rebuilt-po35.wav checked-po35
cmp decoded-po35/frame.bin checked-po35/frame.bin
```

The decoder tries normal and reversed channels and accepts a complete,
checksum-valid frame. `--swap` forces reversed channels. Speech uses the 24 kHz
high bank by default; `--bank low` selects the 8 kHz bank. These rates are
independent of the capture's sample rate.

Exports include settings, patterns, speech headers and raw speech records.
Nonempty speech slots produce `speech-NN.wav` files and an `all-speech.wav`
preview with quarter-second gaps. Each slot is normalized to 0.8 peak, so
relative loudness between slots is not preserved. Channel order, synthesis
settings and normalization are recorded in `summary.txt`.

Empty speech slots retain their header and record exports without a WAV. A
backup with no nonempty speech slots still exports its data successfully and
omits `all-speech.wav`. The summary counts all stored groups in `speech_groups`
and the nonempty per-slot WAVs in `speech_audio_files`.

Without speech edits, the encoder rebuilds the existing frame byte for byte.
Speech previews use an approximate model; exact device voice characters,
effects and gain/noise behavior are not implemented.

## PO-35 / PO-137 Speech Replacement

Replace one or more existing speech slots with mono PCM WAVs:

```sh
./build/core/po35_encode_capture decoded-po35/frame.bin edited-po35.wav \
  --speech 0 spoken-intro.wav --speech 3 spoken-outro.wav --fps 80
./build/core/po35_decode_capture edited-po35.wav checked-edited-po35
```

Slots are zero-based (`0..14`) and must already have a speech header in the
input frame. Repeating `--speech` is allowed for different slots; duplicate
slot edits are rejected. `--fps 20|40|60|80` applies to every replacement and
defaults to 80. `--rate 44100|48000|96000` selects only the transfer WAV rate
and defaults to 48000; it does not change the speech input or frame rate.

Speech inputs must be nonempty mono/stereo PCM16, PCM24, PCM32 or IEEE float32
WAVs at 8–192 kHz, including extensible WAV. Samples must be finite and in
`[-1,1]`. The tool does not clip, truncate or normalize them. Integer PCM is
scaled to float. Stereo speech requires an explicit global channel choice:

```sh
./build/core/po35_encode_capture decoded-po35/frame.bin edited-stereo-po35.wav \
  --speech 2 stereo-speech.wav --channel mix --fps 40 --rate 48000
```

`--channel left` and `--channel right` select one channel; `mix` takes their
arithmetic mean. Mono input needs no channel option, accepts `left` or `mix`,
and rejects `right`. The channel option applies to all supplied speech WAVs.

Each replacement is limited to 640 records. A partial final record is zero-padded:

| Frame rate | Maximum input duration |
| ---: | ---: |
| 80 fps | 8 seconds |
| 60 fps | 10⅔ seconds |
| 40 fps | 16 seconds |
| 20 fps | 32 seconds |

This is the editor's creation policy, not the raw parser's limit. The exact
lower-rate durations follow from `640 / fps` and are not hardware-validated.
See [format limits and source](../docs/PO_FORMATS.md#po-35--po-137).

The editor updates each selected header's record count and frame rate, then
replaces its speech data. It preserves opaque header bytes 4–8, settings,
drum patches, patterns and unselected groups. It rejects incomplete or
inconsistent groups, unknown extensions within a selected group, missing
slots and empty inputs. It cannot create a new slot. Outputs must not exist.

PCM analysis and preview synthesis are approximate models, not the original
Bitspeek or PO-35 implementation. Core synthesis is unnormalized and transient
peaks may exceed `[-1,1]`; the decoder applies the preview normalization
described above. A successful software decode does not establish that an
edited transfer will be accepted or play correctly on hardware.

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

Software roundtrips verify byte preservation. Generated stereo-family
transfers and speech previews have not been validated against physical-device
reception or playback.
