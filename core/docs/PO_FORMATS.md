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

`po32_pattern_control_t` supplies a separate lossless view of each raw trigger:
bits 0..3 are the fill code, bits 4..5 the instrument group, bit 6 remains
unassigned, and bit 7 is accent. Its codecs preserve every byte value; existing
semantic trigger and pattern conversions retain their normalization behavior.
Preserving an unassigned bit does not establish its playback meaning.

## PO-33 / PO-133

The PO-133 uses the same codec family here. Teenage Engineering describes it
as [based on the PO-33](https://teenage.engineering/products/po/capcom), and its
[guide](https://teenage.engineering/guides/po-133/en) documents the same sampler
controls and effect list. The supplied PO-133 bank validates with the shared
decoder: 3,357 packets, sixteen full patterns, 510-byte settings, and 52 sound
headers referencing eight owned pools. This is captured-format compatibility,
not proof of identical firmware or every playback behavior. The physical FX
and control experiments below used a PO-33, not a PO-133.

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
| 386 | 16 | Retained state; first two coordinates have observed pattern/FX-position associations, full meaning unresolved |
| 402 | 108 | Raw suffix; fixed byte pattern in the inspected corpus |

Pattern record: 1032 bytes. Steps and sound indices below are zero-based.

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| `48*step + 3*sound` | 3 per cell | Parameter word, 16 x 16 cells; observed roles below |
| `768 + 16*step + sound` | 1 per cell | Note/key-related low nibble; timing-related high nibble, with melodic length and drum retrigger contexts; exact units unresolved |
| 1024 | 8 | Two FX codes per byte, high nibble first in step order; `6` is octave-up, other effect IDs unresolved |

For zero-based step `s`, its FX code is
`(footer_raw[s / 2] >> ((s & 1) ? 0 : 4)) & 15`. This defines storage order,
not the duration or cross-step behavior of each effect.

For a little-endian word `W`, these chunks are useful descriptive coordinates,
not a declaration that four six-bit parameters are the final encoding.

| Chunk | Extraction | Role and evidence | Remaining limit |
| --- | --- | --- | --- |
| q0 | `W & 63` | Observed pitch control: 13 to 7 lowers playback by about six semitones; 17 to 18 raises it by about one | Full scale, neutral value, width and interaction with the control-note nibble |
| q1 | `(W >> 6) & 63` | Observed level control: repeatable gain-like changes; independent intermediate values also occur without a control event | Exact gain curve and zero/retain behavior |
| q2 | `(W >> 12) & 63` | Observed filter control: frequency-dependent playback changes; cutoff is the leading interpretation | Exact filter mapping and numeric units |
| q3 | `(W >> 18) & 63` | Independently varying stored data; resonance is a candidate | No resolved playback effect in the two tested contexts; identity and activation remain open |

The physical knobs change function with the edit mode selected by FX. The
[PO-33 guide](https://teenage.engineering/guides/po-33/en) documents these
controls and identifies TONE/FILTER as lockable parameters:

| Mode | Knob A | Knob B | Corresponding storage evidence |
| --- | --- | --- | --- |
| TONE | Pitch | Sound volume | q0 has pitch behavior; q1 has gain-like behavior |
| FILTER | High-/low-pass filter | Resonance | q2 has filter-like behavior; q3 as resonance is unconfirmed |
| TRIM | Sound start | Sound length | Sound-header range coordinates, not another per-step q field; exact UI conversion remains unresolved |

Holding a lit step in WRITE mode is a separate editing context. The
[extended control guide](https://cubeleco.blogspot.com/2019/05/po-33-ko-hidden-features.html)
describes knob A as note/slice selection, knob B as melodic note length, and
held-step plus BPM as drum retrigger selection. These UI operations must not
be confused with TONE/FILTER knob automation or TRIM coordinates.

These are control functions and observed storage roles, not a calibrated
conversion from displayed knob values. For example, q1 `32 -> 41` gave about
`+2.77 dB` in one repeatable noise test with an exact bank readback; another
stimulus/recording workflow gave about `+1.61 dB`. Neither supplies a universal
gain law. q2 `47 -> 63` changed the spectral response without establishing a
cutoff in Hz. q3 `26 -> 30` at q2 `7` survived an exact readback, but did not
produce a resolved playback difference. The resonance identity remains open.
Trim start/end coordinates likewise do not establish a knob scale, a direct
stored-length field, or inclusive/exclusive endpoint behavior.

Raw bit 5 is zero in the inspected corpus. Both 6+6+6+6 and 5+7+6+6 layouts
fit; regrouping as 5+7+7+5 also remains possible. Signedness and field boundaries
must not be inferred from the chunk names alone. Each q extraction yields
0..63 by construction; this is not proof of the physical knob's range or a
six-bit field boundary.

Parameter words with zero control are meaningful stored data. Frekvens has a
pattern with 15 q1-only cells and every control byte zero; other intermediate
values match neither neighboring nonzero-control event. Escalate also stores
nonzero q3 on zero-control rows. Preserve these rows: nonzero word does not
establish a note trigger, and zero control does not establish an empty pattern.
These observations do not by themselves prove runtime hold or interpolation.

The old note/Param B control captures leave the three-byte word unchanged and
vary low/high control nibbles. Some nominally adjacent captures also change
pattern selection, tempo, swing or a second event. They are not isolated q3
sweeps or evidence for a nonlinear joint parameter encoding. Their labels do
not by themselves establish calibrated knob values. The held-step editing
context explains why note and melodic timing edits can affect control nibbles
without changing q0..q3. Similarly, A/B/A in
measurement descriptions names comparison variants, not the two physical knobs.

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

The high-nibble datasets cover different sound contexts: the melodic edits are
on sound 0, while the drum retrigger edits are on sound 8. Complete word/control
combinations also occur in both melodic and drum slots. Consequently, the raw
timing nibble is not a universal duration or repeat count; sound context must
accompany a musical interpretation. The structural codecs preserve both nibbles
without inventing a key-number, note-length or retrigger conversion.

Capture names are experiment labels, not calibrated values. For example,
`note_11` and `note_12` contain identical banks in distinct source-audio files.
Historical label-based note maps and retrigger ordinals must not be exposed
as verified key, pitch or repeat-count conversions.

The closest effect-labelled pair changes only pattern 0 byte 1024 from `ff`
to `7f` and settings byte 386 from `00` to `01`. It does not isolate either
field's playback effect. Fresh decoding of both original recordings validates
236 packets apiece and reproduces the same frames. Its step-16 label is not
proof of the stored FX position: the first-byte association tested below places
that high nibble at one-based step 1. Gesture timing, wraparound and label accuracy are
not independently recorded. Raw `7` has no verified musical identity here.
All 48 pattern footers in the inspected official Escalate, Frekvens and
#16operators packs are `ff` bytes, so those packs add
no nondefault footer contrast.

A hardware probe with distinct tones changed high-first footer nibble indices
5..10 to `a` and settings byte 386 from 0 to 11, with all note/control and
sample bytes unchanged. Across 26 complete loops, one-based steps 6/8/10 were
silent and steps 7/9/11 matched the former step-6 tone signature. This favors
direct high-first slot association in that region. Step 12 retained its own
signature with a release transient, so buffering and cross-step behavior
remain relevant. The final changed nibble was high: both high-first-next and
low-first-current position rules predict state 11. This does not identify a
cursor, a complete footer ordering, or the musical name of raw `a`.

A PO-33 hardware test on 2026-11-05 maps footer nibble `6` to octave-up
(FX key 7). During continuous playback, holding FX then key 7 approximately
doubled the measured pitches; releasing the keys restored the normal sequence.
Repeating with WRITE enabled stored the effect, which remained audible for
more than two loops after release. Both before/after backups validated all
423 packets. The only changes were pattern 0 bytes 1024..1031 from `ff` to
`66`, and settings byte 386 from 0 to 11. All note/control bytes, parameter
words, samples and other patterns were unchanged.

The [PO-33 manual](https://teenage.engineering/guides/po-33/en) distinguishes
these recorded performance effects from knob parameter locks. The uniform
`66` footer cannot establish high/low nibble order or a write endpoint, and
state 11 does not distinguish the competing position rules. Only code `6`
has this confirmed musical identity: do not infer all effect codes from key
numbers or reinterpret the older raw `a` captures as octave-up. The clean
baseline uses `f` nibbles; an explicit FX-16 clear-and-readback remains untested.
No intermediate readback separates the live FX control from the recorded FX
interaction, so the state-byte change cannot be assigned to either one alone.

A subsequent asymmetric probe used identical note/control data in patterns
0 and 1, with first footer bytes `f6` and `6f` respectively; every other footer
byte was `ff` and all 16 state bytes initially zero. Across three observations
of each step, `f6` primarily changed one-based note 2, while `6f` primarily
changed note 1. Both had smaller following-note transients. This supports
high-first association for the first byte; short-event pitch estimates did
not reach the sustained octave ratio, so they are not a new pitch scale.
This first-byte probe alone does not establish the remaining byte order or
general cross-step effect behavior.

The complete 423-packet hardware readback preserved all patterns and sample
data. Only settings changed: chain slot 0 selected pattern 1, and state bytes
386/387 became 2/1. For the first byte's low nibble, state 2 fits high-first-next
rather than the previously competing low-first-current rule, which predicts 0.
These were generated footer edits followed by playback, not an isolated manual
FX write. No readback occurred between loading and playback, so that trial
alone cannot distinguish receive/send normalization from a runtime update.

A later PO-33 probe tested all 16 nibble positions in an automatic chain of
16 patterns. Each pattern contained one code `6` at a different position and
three separated pitch markers for independent pattern identification. A
200-second capture had no timestamp gaps or dropped frames. On both channels,
at least three complete loops of each pattern placed the largest note change
at its high-first target step, covering all eight footer bytes. The selected
notes also shortened relative to the clean reference. This establishes the
storage order above; short-event transients still differ from sustained FX.

Complete 423-packet readbacks immediately after loading and after chained
playback were both byte-identical to the generated bank. The state vector
remained fifteen zeros followed by `7`, despite every stored FX event being
exercised. The final nonzero coordinate makes the unchanged result observable
even for a proposed wrap to zero. Thus zero does not disable stored FX in this
flow, and these bytes do not unconditionally track playback or the last FX
position. The earlier manual-selection/FX interactions remain distinct cases.

The user subsequently identified that chained run as WRITE enabled. A repeat
with WRITE explicitly off, using the same loaded bank, returned the identical
68,450-byte frame with all 423 packets valid. Thus the exported state had no net
change in either reported mode for this chained flow. Endpoint backups do not
exclude transient runtime changes or establish update rules for other gestures.

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

Settings bytes 386..401 retain state with observed values 0..15. Nonzero values
can remain when no pattern events are present. Conversely, populated patterns
can have all-zero state bytes. This rules out a simple event-occupancy map but
does not prove LED brightness, spatial order or snapshot timing. The asymmetric FX
probe above associates the first two coordinates with patterns 0 and 1 and
their FX positions; the remaining coordinates and update timing are unverified.

Rechecking 237 archived sampler identities (187 distinct frames) also finds
nonzero coordinates alongside all-`ff` footers, and nondefault footers alongside
zero coordinates. Thus these bytes are not a current FX count or occupancy map
and cannot be derived solely from the current footer. The old FX pair and new
asymmetric probe show FX-position associations, with values 1 and 2 associated
with the first and second FX slots. The chained playback test above leaves
every coordinate unchanged while stored FX executes, so a retained FX-position
model must have additional update conditions. These observations do not establish
a general update rule,
wrap behavior, or whether the value denotes a current or next position under
the device's indexing convention. Preserve it when editing unrelated fields.

A separate hardware interaction changed only settings byte 386 from 0 to 6;
every other logical byte matched the baseline. This state coordinate therefore
cannot be derived solely from the other serialized content in those captures.
The observation does not establish its meaning or the ownership of the other
fifteen coordinates.

In another hardware readback, clock-driven playback with all-`ff` footers left
the entire bank, including these 16 bytes, identical to the clean baseline.
After the subsequent octave-up procedure, byte 386 was 11 as described above.
These observations do not support a continuously serialized playback position in
that test, but do not establish a write cursor or other retained-state meaning.
Automatic chained playback and the earlier manually selected patterns also
differed in their control sequence. The chained WRITE-on/off comparison above
does not explain the earlier state changes; selection-side updates, explicit FX
gestures and the earlier trial's mode remain unisolated. Do not attribute the
difference to one cause.

Some legacy single-channel "decoded" grids were still projections of
whitened stereo wire bytes. In three verified old exports, the range formerly
called `132:516` maps mostly to pattern-event payload plus packet overhead,
not a global display buffer. Compare offsets only after full stereo packing,
dewhitening and record reassembly. The actual 16-step by 16-sound records
contain editable parameter/control data and must not be discarded as a frozen
LED image.

## PO-35 / PO-137

The retained PO-35/137 corpus uses one shared backup grammar and record layout.
The [PO-35 guide](https://teenage.engineering/guides/po-35/en) and
[PO-137 guide](https://teenage.engineering/guides/po-137/en) list the same eight
voices, effect controls and four adjustable speech parameters. This is not
proof of identical table contents, firmware or rendered audio. The two public
reference banks have different drum patches, patterns and speech, as well as
differences in four of eight `7BE7` tables and two of eight `1C7F` tables.
Offline transfer roundtrips preserve both references; physical cross-loading
and exact synthesis parity remain unverified.

Tags: `37B2` drum patches; `7BE7` and `1C7F` indexed tables; `CCB2` pattern,
`29FF` empty pattern; `DC32` speech header, `73B0` records, `29A1` section end;
`84D8` settings. The indexed tables' semantics remain unresolved.

Both indexed-table layouts are fixed in the inspected corpus:

| Tag | Payload bytes | Layout |
| --- | ---: | --- |
| `7BE7` | 9 | One index byte followed by eight uninterpreted bytes |
| `1C7F` | 13 | One index byte followed by twelve uninterpreted bytes |

All 800 records of each tag across 100 retained capture identities match these
sizes. The table codecs preserve the full index byte and every body byte rather
than restrict values to observed indices 0..7. The nonzero bytes of `1C7F` can
resemble three little-endian integers, but that does not establish integer
fields instead of bytes with padding. Desktop Bitspeek enums and candidate
gain/detune interpretations are not imposed on either table.

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

Gain and unvoiced values remain unsigned raw codes. The unvoiced byte spans
254 distinct values in the inspected speech bodies, so it is not a boolean.
Bitspeek analysis informs the reconstruction model but does not establish
device gain/noise laws, header metadata units or equivalent voice selectors.
Zero excitation gain also does not guarantee immediate zero PCM when a
renderer retains filter history; no silence predicate or forced terminator is
inferred from the record values.

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
