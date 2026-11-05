# Pocket Operator backup formats

These layouts describe the records handled by the C codecs. Fields marked raw
must survive edits unchanged unless explicitly selected. Model numbers identify
compatible formats; the data does not authenticate the device that produced it.

**Structural** descriptions below are verified record layouts and byte
relationships. **Observed roles** have controlled capture or playback support,
but may lack a complete numeric scale. **Candidates** remain unconfirmed.
Keeping a field raw in the API does not mean nothing is known about it: it
preserves values that the semantic model cannot yet reproduce safely.

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

The families share a packet envelope and the concepts of sounds, patterns and
settings, not one interchangeable file layout:

| Data | PO-32 | PO-33 / PO-133 | PO-35 / PO-137 |
| --- | --- | --- | --- |
| Sound representation | Pairs of drum patches | Companded pools and sample-range references | Speech records plus pairs of drum patches |
| Full pattern | 211 bytes; four trigger/parameter lanes | 1032 bytes; 16 steps by 16 sounds | 183 bytes; separate speech/drum event and parameter arrays |
| Pattern identity | Inside the pattern payload | Separate selector packet | Inside the pattern payload |
| Settings | 35 bytes plus chain entries | Fixed 510-byte record | 156 bytes plus chain entries |

The incrementing PO-33/35 preamble is transport synchronization data. A regular
grid in a dump of those bytes is not by itself an LED or pattern bitmap.

In the inspected full packs, PO-33 settings precede its patterns and sample
groups; PO-32 and PO-35 settings follow their sound/pattern records. Shared
framing therefore does not imply shared record order or payload offsets.

## Compared pack shapes

The inspected [official PO-32](https://teenage.engineering/downloads/po-32)
and [official PO-33](https://teenage.engineering/downloads/po-33) downloads
illustrate different complete and partial content sets:

| Pack or reference | Sound data | Pattern data |
| --- | --- | --- |
| PO-32 Basic Beats 1/2 | 32 patch endpoints, paired for 16 instruments | 16 full patterns |
| PO-32 Flat Beat | 32 patch endpoints | 15 full patterns and one raw slot marker |
| PO-32 CPU packs | 16 patch endpoints, paired for eight instruments | One full pattern |
| PO-33 Escalate | 136 headers referencing 16 owned pools | 16 nonempty records |
| PO-33 Frekvens | 85 headers, 39 owned pools; 51 selectors without data | 15 nonempty records and one empty record |
| PO-33 #16operators | 136 headers referencing 16 owned pools | 16 empty records |
| Supplied Streetfighter / PO-133 | 52 headers, eight owned pools; 84 selectors without data | 16 nonempty records |
| Public PO-35 and PO-137 references | 32 drum endpoints, two sets of eight indexed tables, 15 speech groups | 16 full patterns |

Here sampler "nonempty" means a nonzero parameter word or control byte, not
an asserted count of audible notes. Empty full records, one-byte empty/slot
markers, and omitted data are distinct. A selector without data is also
different from a header-only sample reference. Omission does not specify a
receiver's clear, retain or default behavior.

The PO-133 reference is user-supplied. The public references were posted by
becVision as [PO-35](https://op-forums.com/t/19078/19) and
[PO-137](https://op-forums.com/t/19078/17), with the latter labelled
"Stereo Boosted." These are uploader attributions, not manufacturer
authentication or proof of unprocessed, first-generation audio. Partial
PO-35 controls also exist with drum endpoints and indexed tables but only an
empty-pattern marker and no speech groups. Identical container and group
shapes do not make the sound codecs interchangeable.

## PO-32

See [the protocol reference](PROTOCOL.md) for tags and offsets. The one-byte
`496F` pattern-slot record appears in place of full pattern records. A
[captured regression fixture](../tests/test_core.c) contains marker `03` and
state chain `[03]`, so a marked slot can be in the chain. Its precise receive
action remains unmodeled; raw parsing preserves it.

The 16 pattern bytes at offsets 193..208 contain independently varying data,
not disposable padding. Official pattern pairs differ only in this region
apart from their slot number. A per-step effect/control role is a candidate;
effect names and high-nibble behavior are not established. The trailing accent
mask can contain bits beyond the active trigger accents.

Two limits apply to the current typed API, rather than to the wire format:

- `po32_state_packet_t` holds 16 chain entries. Official Basic Beats 1 contains
  22 ordered entries with repetitions: raw decoding preserves them, but typed
  state decoding rejects them.
- Typed pattern encoding writes zero for an inactive instrument. Some valid
  captures retain selector/accent bits in otherwise inactive trigger bytes,
  so a typed decode/encode is not always byte-identical. Raw packets preserve
  those bytes. The reserved region and accent mask are retained by the typed
  codec.

The raw PO-32 parser currently admits state payloads of 35..70 bytes. That is
an implementation bound, not a measured maximum chain length for the device.

## PO-33 / PO-133

Tags: settings `7D5C`; pattern selector `A041`, data `C498`; sound selector
`68B5`, data `EF78`. Logical records can span several packets. Preserve the
original packet boundaries when reproducing a captured frame.

Settings record: 510 bytes.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 255 | Raw prefix; fixed byte pattern in the inspected corpus |
| 255 | 128 | Stored pattern-chain slots, including unused entries |
| 383 | 1 | Active chain length |
| 384 | 1 | Tempo |
| 385 | 1 | Raw swing amount |
| 386 | 16 | Unassigned retained state; ownership and UI relationship unresolved |
| 402 | 108 | Raw suffix; fixed byte pattern in the inspected corpus |

Pattern record: 1032 bytes. Steps and sound indices below are zero-based.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| `48*step + 3*sound` | 3 per cell | Parameter word, 16 x 16 cells; observed roles below |
| `768 + 16*step + sound` | 1 per cell | Note/key-related low nibble; retrigger-associated high nibble, exact count/duration meaning unresolved |
| 1024 | 8 | Effect-associated raw footer; packing and effect IDs unresolved |

For a little-endian word `W`, these chunks are useful descriptive coordinates,
not a declaration that four six-bit parameters are the final encoding.

| Chunk | Extraction | Role and evidence | Remaining limit |
| --- | --- | --- | --- |
| q0 | `W & 63` | Observed pitch control: 13 to 7 lowers playback by about six semitones; 17 to 18 raises it by about one | Full scale, neutral value, width and interaction with the control-note nibble |
| q1 | `(W >> 6) & 63` | Observed level control: repeatable gain-like changes; independent intermediate values also occur without a control event | Exact gain curve and zero/retain behavior |
| q2 | `(W >> 12) & 63` | Observed filter control: frequency-dependent playback changes; cutoff is the leading interpretation | Exact filter mapping and numeric units |
| q3 | `(W >> 18) & 63` | Independently varying stored data; resonance is a candidate | No resolved playback effect in the two tested contexts; identity and activation remain open |

Raw bit 5 is zero in the inspected corpus. Both 6+6+6+6 and 5+7+6+6 layouts
fit; regrouping as 5+7+7+5 also remains possible. Signedness and field boundaries
must not be inferred from the chunk names alone.

Parameter words with zero control are meaningful stored data. Frekvens has a
pattern with 15 q1-only cells and every control byte zero; other intermediate
values match neither neighboring nonzero-control event. Escalate also stores
nonzero q3 on zero-control rows. Preserve these rows: nonzero word does not
establish a note trigger, and zero control does not establish an empty pattern.
These observations do not by themselves prove runtime hold or interpolation.

The old note/Param B control captures leave the three-byte word unchanged and
vary low/high control nibbles. Some nominally adjacent captures also change
pattern selection, tempo, swing or a second event. They are not isolated q3
sweeps or evidence for a nonlinear joint parameter encoding.

For the comparisons below, `q0 = W & 63`, where `W` is the little-endian
event word. This is a descriptive coordinate, not a proven field width.

The archived `factory`-labelled capture also has q0=14 on a zero-control row
between two q0=13 control events (pattern 6, sound 9, steps 12..14). Thus
copying either adjacent control event cannot explain all stored q0 values.
The capture label does not independently authenticate factory provenance.

Four captured banks contain controls `fc`, `1c`, `10` and `f0` at pattern 0
offset 768, with every other logical byte identical. This establishes that
the two nibbles can vary independently over those observed combinations.
Six other matched pairs retain word `0d f8 83` and controls
`19`, `29`, `39`, `49`, `69`, `89` when relocated between steps 6 and 15,
sound 8 (zero-based). Moving only the word and control produces the entire
counterpart bank exactly. The stored codes therefore do not require a
step-dependent lookup for those two positions; audible repeat counts remain
unverified.

Capture names are experiment labels, not calibrated values. For example,
`note_11` and `note_12` contain identical banks in distinct source-audio files.
Historical label-based note maps and retrigger ordinals must not be exposed
as verified key, pitch or repeat-count conversions.

The closest effect-labelled pair changes only pattern 0 byte 1024 from `ff`
to `7f` and settings byte 386 from `00` to `01`. It does not isolate either
field's playback effect. Its step-16 label is insufficient to establish
footer order or effect IDs. All 48 pattern footers in the inspected official
Escalate, Frekvens and #16operators packs are `ff` bytes, so those packs add
no nondefault footer contrast.

Sound header: nine bytes: LE32 range start, LE32 range end, source selector.
These are raw coordinates: inclusive/exclusive conventions and all range
constraints remain unresolved. Populated pools contain following mu-law bytes
in 4096-byte blocks. Copy/slice entries may have only a header and refer to
another pool. Typically selectors 0..7 are melodic; 8..135 form eight groups
of sixteen drum slices. Copy operations can reference a different group's
source. Stored ranges can exceed the received allocation; encoders preserve
these values without clamping them.

A source selector refers to the entire stored pool, not its owner's trimmed
range. Aliases can extend outside that owner's range, and a drum group can
contain multiple owned pools. Direct low-nibble slice indexing fits inspected
official patterns better than XOR 12 under an audible-slice assumption, but
other mappings survive; no exact key-to-slice map is exposed here.

Escalate's out-of-allocation references belong to a sound unused by all its
patterns. Retained earlier ranges are plausible, not established. This does
not justify clamping, wrapping or halving coordinates. Nonzero samples can
occur outside trims and at touching endpoints; silence is not a universal
boundary marker and does not settle inclusive versus exclusive ends.

For populated pools, the first 4,096 sample bytes and nine-byte header are
packetized as sixteen 255-byte payloads followed by 25 bytes. Each additional
4,096-byte block uses sixteen 255-byte payloads followed by 16 bytes. The hosted
editor retains original packets and follows this layout when appending blocks.
It keeps all nine header bytes and all reference headers unchanged, padding
short replacements with silence rather than shrinking the original allocation.
The original trim can therefore hide appended audio. Total device storage
capacity is not established; a complete software roundtrip is not proof that a
larger allocation fits or restores on hardware.

`po33_settings_*`, `po33_pattern_*` and `po33_sound_header_*` provide structural
codecs. They preserve raw fields; they are not a complete sequencer or editor.

### Early grids and possible display state

The literal frame prefix is the fixed counter/sync preamble. The settings
prefix and suffix also follow fixed arithmetic templates in the inspected
corpus. Their purpose is not fully established, but their visual regularity
is not evidence of captured LED levels.

Settings bytes 386..401 are a more plausible retained-display candidate:
there are 16 bytes, with observed values 0..15, and nonzero values can remain
when no pattern events are present. Conversely, populated patterns can have
all-zero state bytes. This rules out a simple event-occupancy map but does not
prove LED brightness, spatial order or snapshot timing. Retained selection or
effect state remains another explanation.

Some legacy single-channel "decoded" grids were still projections of
whitened stereo wire bytes. In three verified old exports, the range formerly
called `132:516` maps mostly to pattern-event payload plus packet overhead,
not a global display buffer. Compare offsets only after full stereo packing,
dewhitening and record reassembly. The actual 16-step by 16-sound records
contain editable parameter/control data and must not be discarded as a frozen
LED image.

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
| 153 | 2 | Raw retained state; byte 154 is a candidate selected/remembered sound ID |
| 155 | 1 | Pattern-chain entry count |
| 156 | Count | Zero-based pattern indices, repeats preserved |

The single-packet typed codec supports at most 99 chain entries because the
packet payload limit is 255 bytes.

Pattern payload: 183 bytes.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 1 | Pattern index |
| 1 | 16 | Speech event bytes |
| 17 | 16 | Raw secondary speech state; note/duration nibbles are candidates |
| 33 | 64 | Four parameter bytes per step |
| 97 | 16 | Drum events |
| 113 | 32 | Interleaved per-step drum pitch/morph |
| 145 | 16 | Raw index pairs; voice/effect nibbles are candidates |
| 161 | 16 | Raw step state |
| 177 | 4 | Raw footer |
| 181 | 2 | Stored derived mask, little-endian; preserved, not recalculated |

Across observed continuation-like speech events, candidate note and
voice/effect fields and parameter values can change; one captured transition
also changes slot. These events must not be
expanded as copies of the previous event. Inactive rows can retain secondary
state. The mask is not a simple active-step or accent bitmap; preserve it
without imposing a guessed formula on new data.

Speech header: slot byte, BE16 record count, frame-rate byte, five raw metadata
bytes. Each following record is 44 bytes: signed LE16 pitch, gain byte,
unvoiced byte, ten signed low-bank LAR values, thirty signed high-bank values.
In the reconstruction model, pitch is signed Q11 octaves relative to C4.
The low and high filter banks use 10 and 30 coefficients at 8 and 24 kHz.
Gain, noise, voice characters and effects are not fully mapped. See the
[speech analysis API](C_API.md#po-35-speech-analysis) and
[speech renderer](C_API.md#po-35-speech-rendering) for model limits.

All 56 distinct speech bodies in the inspected corpus end with zero gain and
the previous record's other 43 bytes. Interior silent records have exceptions.
This is evidence of held analysis state during silence and a recurring ending,
not a mandatory sentinel or proof of an append-versus-overwrite rule. The PCM
encoder does not claim to reproduce exact device release behavior.

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

The PO-33/35 structural codecs preserve record bytes; the PO-32 typed limits
above require raw packets for exact preservation in some cases. Observed field
roles do not establish complete numeric laws. Selected rebuilt PO-33 backups
and a PCM sample replacement have user-confirmed device acceptance, and
selected test banks have byte-identical device readbacks. Those observations
do not validate every input, larger allocation, PO-133 hardware, or generated
PO-35/137 speech. Software roundtrips and physical acceptance remain separate
evidence.
