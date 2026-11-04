# Pocket Operator backup formats

These layouts describe the records handled by the C codecs. Fields marked raw
must survive edits unchanged unless explicitly selected. Model numbers identify
compatible formats; the data does not authenticate the device that produced it.

## Transport families

| Family | Preamble | End | Modem |
| --- | --- | --- | --- |
| PO-32 | 124 x `55`, then `14 19 9D CF` | Special five-byte state tail | Mono DPSK |
| PO-33 / PO-133 | 4092 incrementing bytes modulo 256, then `4D B8 16 5D` | Zero-length `826F` packet | Stereo DQPSK |
| PO-35 / PO-137 | Same 4096-byte counter/sync | Same special tail as PO-32 | Stereo DQPSK |

All use additive whitening and the running CRC state initially `1D0F`, with
little-endian 16-bit tag, 8-bit payload length, payload and two-byte state
trailer. State continues across packets. This is a transport checksum, not
cryptographic authentication. PO-33 begins with a zero-length `985B` packet;
PO-35 does not. Shared mechanics are in `po_wire_internal.h`; public family
parsers retain their different grammar and acceptance rules.

## PO-33 / PO-133

Tags: settings `7D5C`; pattern selector `A041`, data `C498`; sound selector
`68B5`, data `EF78`. Logical records can span several packets. Preserve the
original packet boundaries when reproducing a captured frame.

Settings record: 510 bytes.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 255 | Opaque prefix |
| 255 | 128 | Stored pattern-chain slots, including unused entries |
| 383 | 1 | Active chain length |
| 384 | 1 | Tempo |
| 385 | 1 | Raw swing amount |
| 386 | 16 | Unresolved state |
| 402 | 108 | Opaque suffix |

Pattern record: 1032 bytes. Steps and sound indices below are zero-based.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| `48*step + 3*sound` | 3 per event | Unresolved event word, 16 x 16 cells |
| `768 + 16*step + sound` | 1 per event | Raw control byte; low nibble is note-related |
| 1024 | 8 | Unresolved footer |

The event word, high control nibble and parameter scaling are not fully mapped.

Sound header: nine bytes: LE32 range start, LE32 range end, source selector.
These are raw coordinates: inclusive/exclusive conventions and all range
constraints remain unresolved. Populated pools contain following mu-law bytes
in 4096-byte blocks. Copy/slice entries may have only a header and refer to
another pool. Typically selectors 0..7 are melodic; 8..135 form eight groups
of sixteen drum slices. Copy operations can reference a different group's
source. Stored ranges can exceed the received allocation; encoders preserve
these values without clamping them.

`po33_settings_*`, `po33_pattern_*` and `po33_sound_header_*` provide structural
codecs. They preserve raw fields; they are not a complete sequencer or editor.

## PO-35 / PO-137

Tags: `37B2` drum patches; `7BE7` and `1C7F` indexed tables; `CCB2` pattern,
`29FF` empty pattern; `DC32` speech header, `73B0` records, `29A1` section end;
`84D8` settings. The indexed tables' semantics remain unresolved.

The 43-byte drum payload is identical to PO-32: selector followed by 21 LE16
parameters normalized by 65535. Left/right selectors are `0x10 | (slot-1)` and
`0x20 | (slot-1)`. There are sixteen pairs of patches, not sixteen simultaneous
hardware voices. Reuse the existing PO-32 patch codec and synth.

Settings payload: 156 bytes plus active pattern-chain entries.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 120 | Fifteen 8-byte speech-control records; first four parameter bytes have pattern counterparts, remaining four are raw state |
| 120 | 32 | Sixteen interleaved drum pitch/morph pairs |
| 152 | 1 | Tempo |
| 153 | 2 | Unresolved state |
| 155 | 1 | Pattern-chain entry count |
| 156 | Count | Zero-based pattern indices, repeats preserved |

The single-packet typed codec supports at most 99 chain entries because the
packet payload limit is 255 bytes.

Pattern payload: 183 bytes.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 1 | Pattern index |
| 1 | 16 | Speech event bytes |
| 17 | 16 | Raw secondary speech state |
| 33 | 64 | Four parameter bytes per step |
| 97 | 16 | Drum events |
| 113 | 32 | Interleaved per-step drum pitch/morph |
| 145 | 16 | Raw index pairs; semantics unresolved |
| 161 | 16 | Raw step state |
| 177 | 4 | Raw footer |
| 181 | 2 | Stored derived mask, little-endian; preserved, not recalculated |

Speech header: slot byte, BE16 record count, frame-rate byte, five raw metadata
bytes. Each following record is 44 bytes: signed LE16 pitch, gain byte,
unvoiced byte, ten signed low-bank LAR values, thirty signed high-bank values.
In the reconstruction model, pitch is signed Q11 octaves relative to C4.
The low and high filter banks use 10 and 30 coefficients at 8 and 24 kHz.
Gain, noise, voice characters and effects are not fully mapped. See the
[speech analysis API](C_API.md#po-35-speech-analysis) and
[speech renderer](C_API.md#po-35-speech-rendering) for model limits.

The raw header can represent up to 65,535 records. The hosted speech editor
uses a separate 640-record creation policy: 8 seconds at 80 fps, 10⅔ seconds
at 60 fps, 16 seconds at 40 fps or 32 seconds at 20 fps. Magnus Lidström
[documents the four rates, the 80 fps default and longer recording at lower rates](https://soniccharge.com/forum/topic/1060-po-35-secret-powers).
The durations above follow from `640 / fps`; the exact lower-rate limits are
an inference used by the editor, not verified hardware acceptance limits.
Parsers retain larger valid raw groups without imposing this creation policy.

Speech replacement preserves the slot and opaque header bytes 4–8, updates
only the BE16 record count and frame-rate byte, and replaces the group's
record packets. It retains settings, drum patches, patterns and other groups.
Unknown extensions inside a selected group make replacement ambiguous and
are rejected by the hosted editor.

## Limits

The typed codecs preserve record bytes; they do not establish every field's
musical meaning. Trim conventions, voice/effect controls and some pattern
parameters remain unresolved. Software roundtrips do not establish physical
device acceptance.
