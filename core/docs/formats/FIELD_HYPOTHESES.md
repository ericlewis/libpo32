# What the remaining fields could mean

[Family relationships](FAMILY_RELATIONSHIPS.md) · [PO-33/133](PO33.md) · [PO-35/137](PO35.md)

The most useful working model is **musical events plus retained editing state**.
These backups preserve more than the minimum needed to play a sequence. Several
inactive cells retain selectors or secondary data, and some state changes during
manual interactions without changing the music. A common implementation design
is plausible; identical field meanings across devices are not.

This is a hypothesis guide, not a binary specification. “Leading” means the best
current explanation among alternatives, not a verified field name. Raw codecs
must continue preserving unresolved bytes. No production encoding or synthesis
behavior changes follow from this guide.

## Ranked interpretations

| Field | Leading interpretation | Confidence and competing explanation |
| --- | --- | --- |
| PO-35 settings byte 154 | Selected or last-used instrument in a combined speech/drum ID space | Strong candidate for numbering; update timing unresolved |
| PO-35 pattern mask at 181..182 | Narrow reference summary derived from that combined ID space | Exact corpus fit; purpose and firmware arithmetic unproven |
| PO-35 step state at 161..176 | Effect origin/reference or retained effect-edit history | Strong association with the suspected FX nibble; two counterexamples to a universal lookup |
| PO-35 speech event low code `F` | Continuation/state update rather than a fresh ordinary onset | Strong structural and UI support; acoustic retrigger behavior unresolved |
| PO-33 state at 386..401 | Per-pattern retained FX/editor position or cached state | Better fit than a live playhead or occupancy map; update conditions unresolved |
| PO-33 q3 | Resonance-related control | Plausible from the four knob functions; tested playback differences remain unresolved |
| PO-35 footer byte 177 | Packed pattern key and scale | Plausible placement; two bit partitions remain indistinguishable |
| PO-35 header byte 4 | Sample-associated BPM or timing reference | Better fit than current global BPM; no isolated sample-BPM edit |
| PO-35 indexed tables | Voice/excitation and performance-effect definitions | Eight-entry organization fits the UI; field widths and most numeric laws unresolved |
| PO-32 pattern bytes 193..208 | Per-step performance-effect/control state | Independently varying bytes; no complete musical mapping |

Offsets are zero-based within decoded logical payloads. PO-33's record offsets
are not packet offsets. Model-variant compatibility supports using one family
codec; PO-35 findings are not independent physical validation on PO-137, nor
PO-33 findings on PO-133.

## 1. A combined instrument ID explains two PO-35 clues

The device offers fifteen speech slots and a Tonic section with sixteen drum
keys. The controlled drum-key comparison maps keys 1 and 9 to event selectors
0 and 8, while settings byte 154 changes from 15 to 23. This fits:

```text
speech ID = speech slot index       // candidate IDs 0..14
drum ID   = 15 + drum key index     // candidate IDs 15..30
```

Independently, the stored pattern mask fits the low sixteen bits of the union
of `1 << ID` for active events in all 62 distinct retained patterns. That
would retain drum key 1's bit and lose the higher drum IDs. The two observations
make a shared internal instrument numbering scheme more plausible than either
alone. A reference summary could support selective export, UI bookkeeping or
another cached operation; its consumer is unknown.

Do not call this a recovered firmware bug. A special case for drum key 1 also
fits the available mask observations. Do not use the mask as an activity test:
the entered key-9 event has a zero stored mask. The smallest useful additional
distinction is an existing or future key-1 event with multiplier 2: it separates
a selector-based rule from a test for the exact raw event byte `01`.

Evidence: [drum-event census and hardware comparison](PO35_DRUM_EVENTS.md).

## 2. Effect-related references are more likely than generic step numbers

A fresh 2026-11-07 native-parser audit hash-checked 100 archived identities,
deduplicated them to 89 frames and 62 full pattern payloads, and tested the
following relationship. Let `r = pattern[161+s]`; compare the index pair at
`145+s` with the pair at `145+r`.

All 992 values of `r` are in 0..15. The referenced pair's high nibble matches
the current high nibble at 990 positions; the low nibble matches at only 711.
Because many patterns are constant, the more useful comparison restricts the
population to the eleven patterns whose high nibble varies:

| Candidate reference | Matching high nibbles / 176 positions |
| --- | ---: |
| Stored byte at `161+s` used as a zero-based index | 174 |
| Always step index 0 | 98 |
| Previous step, wrapping within the pattern | 121 |

Both exceptions are in pattern index 9 at step indices 2 and 3. They store
reference 14, have high nibble 7 themselves, and point to high nibble 3. Both
have active speech event `A1`. Pattern payload SHA-256:
`58c5c5fc715a71c76cc4e4e249038fec160aa6211a0b5250b250dee76ccb7c11`.

The leading explanation is an **effect origin or history reference**, possibly
used to restart an effect's phase, interpolate a change, or retain edit state.
If high code 7 is the no-effect/default state, the exceptions could be stale
references after clearing FX. That is a testable explanation, not a finding
that either clearing or phase reset occurred. An authoring cache that playback
does not consult is still possible. Positions within patterns are correlated;
174/176 is a corpus fit, not a probability of correctness.

An isolated FX clear/edit pair would distinguish stale history from a live
reference. Playback with equal effect codes but different references would
distinguish a renderer input from export-only bookkeeping. Until then, do not
recompute this array or resolve every step through it.

Earlier analysis also found 38 references outside the current effect run's
start and different reference arrays for equal effect-code sequences. A simple
"rebuild every reference from the current run start" model is already ruled
out by those stored examples. An origin model would need retained history or
another update condition; renaming the field to `effect_start` would be premature.

## 3. Sustained speech can change while it continues

Existing analysis finds all 367 linear low-code-`F` speech events follow an
active event with a full secondary low nibble. Across same-slot continuations,
notes, voice indices and parameter rows can change. One boundary changes slot
too. This supports a continuing voice with per-step state updates, rather than
“repeat the previous event unchanged.”

The [PO-35 developer's notes](https://soniccharge.com/forum/topic/1060-po-35-secret-powers)
describe notes spanning chained patterns and live sample substitution that
retains parameter movements. They also describe reusing the last performed
note's settings and length when placing a step. Those behaviors explain why
current slot defaults need not match an older pattern, and why inactive
secondary state can survive an event move. They do not determine the exact
gate duration, onset phase or interpolation law.

This makes a renderer based solely on isolated note-on events a weak model.
A better experimental model separates onset, continuation, gate, note and
parameter updates. No public enum for every low code follows yet. The drum
multiplier is a separate context: the clean repeated edit changed only `81`
to `82`; the original extra step-3 byte is not a required endpoint marker.
See [the rejected marker prediction](PO35_MULTIPLIER_STATE.md).

## 4. PO-33 state is probably retained, but its ownership is not settled

The sixteen settings bytes fit per-pattern FX/editor state better than a
continually exported playhead, effect occupancy or LED snapshot. They survived
automatic chained playback unchanged with WRITE reported both on and off,
yet manual selection/FX/export interactions changed particular coordinates.
Some nonzero coordinates coexist with clear FX footers and vice versa.

Plausible implementations include a remembered FX-write endpoint, selected
pattern edit cache, or value copied from transient state at particular UI
transitions. “Next FX position” explains some observations but is not a
general update rule. A before/after SEND cannot reveal every transient state
change that happens between those endpoints.

The analogy to PO-35 is architectural: both may serialize editing history.
PO-33's sixteen bytes are global settings with pattern associations; PO-35's
sixteen step-state bytes live inside each pattern. They are not the same array.
Evidence and contrary cases: [PO-33 retained-state observations](PO33_EFFECTS_STATE.md).

## 5. Guess control roles before guessing exact scales

**PO-33 q3:** resonance is the leading role because the official four lockable
knob functions align with the observed pitch, level and filter behavior of the
other chunks. The null q3 playback tests lower confidence in a simple direct
mapping. Conditional filter behavior, weak sensitivity in the tested contexts,
or different bit packing remain alternatives. Six-bit extraction does not prove
six-bit fields: 6+6+6+6, 5+7+6+6 and 5+7+7+5 still need discrimination.
The [parameter guide](PO33.md#pattern-parameters-and-controls) preserves these limits.

**PO-35 key/scale:** byte 177 can be partitioned as signed key shifted by two
plus two scale bits, or shifted by three plus three low bits. Raw `2A` gives
key +10 versus +5; `A1` gives -24 versus -12. Both fit because observed bit 2
is always clear. A musically tidy range is a prior, not proof. One known key
increment is more informative than many unrelated factory patterns.

**PO-35 sample BPM:** header byte 4 stays unchanged in a global-tempo-only pair,
which rejects a direct current-tempo copy. A sample-associated timing reference
fits the documented separate sample-BPM control and tempo-matched playback.
Recording-time tempo, user-edited sample BPM and another timing parameter remain
different hypotheses. Speed, frame rate, record count and sample BPM must not
be collapsed into one length field. See [header evidence](PO35.md).

**Other PO-35 defaults and metadata:** the four state bytes after each sound's
four settings controls could retain note, length, voice and FX defaults used
when entering new steps. The documented copying behavior supports that role,
but not a four-byte assignment by elimination. Header bytes 5 and 8 are level
and noise-balance candidates because those hidden sample controls exist; their
order and scales need independent evidence.

**Other PO-35 footer bytes:** byte 178's observed `00`, `77` and `98` could be
packed editor defaults or retained state. Despite `77` resembling a default
voice/FX pair, `98` exceeds the observed 0..7 domains of both index nibbles;
a direct copy of that pair is a poor model. It also cannot be derived solely
from byte 177 or copied directly from current global tempo/swing. Bytes 179
and 180 are zero in all 62 patterns: reserved fields, padding and state never
exercised by this corpus remain indistinguishable. Zero-only data cannot name
the dormant behavior.

## 6. Bitspeek narrows mechanisms, not the PO byte map

The [Bitspeek description](https://soniccharge.com/bitspeek) supports a model
with separate pitch, level, voicing and spectral-envelope information. The old
desktop implementation analysis adds excitation modes, noise mixing, pitch
tracking and detune as candidate mechanisms for the PO voice definitions.

Two useful numeric leads from that research remain hypotheses:

- A candidate table value `19114 / 32768` is almost exactly seven semitones in
  octave units. Passing the same normalized number through the desktop detune
  knob curve gives about 8.18 semitones. Direct fixed-point interval storage is
  a better candidate than copying that knob curve, assuming this is the fifth
  voice's interval field. It does not identify neighboring fields by analogy.
- Table `7BE7` payload byte 1 has values `0,2,4,1,1,1,1,1` by index. A mode enum
  or flags controlling pitch tracking/excitation is plausible. Value 4 rejects
  direct reuse of the desktop four-mode sample-rate index. A root-bypass bit
  interpretation additionally depends on the proposed voice order and the
  community claim that only three voices respond to root tuning.

Desktop mode initialization has four 20-byte descriptors expanded into 112-byte
runtime records. The PO tables have eight entries with eight- or twelve-byte
bodies. Shared algorithms can have different parameters, numeric scales and
serialization. These differences rule out a direct descriptor-layout copy,
while leaving adapted DSP mechanisms plausible.

For the speech gain byte and header level metadata, distinguish common analyzed
level, bank-specific residual normalization and user gain. Existing correlations
do not select linear versus logarithmic coding. Intelligible approximate audio
does not validate the exact device gain/noise laws.

## 7. PO-32: packed FX history, accent state and the misleading `flag` name

PO-32's shared 43-byte drum patch is well established; its remaining sequencer
fields still deserve the same hypothesis-driven treatment. A fresh audit of
thirteen official download entries, deduplicated to twelve complete frames,
rechecked hashes and native parsing and examined 56 full patterns. All 56
payloads remain distinct after omitting their pattern-index byte. No waveform
was newly demodulated and no physical PO-32 was tested in this pass.

### The extra sixteen bytes may pack FX code and history together

For the byte at pattern offset `193+s`, the leading new model is:

```text
low nibble  = candidate effect code
high nibble = candidate origin/reference step
```

This could pack into one byte the concepts stored in separate PO-35 arrays.
The official PO-32 guide documents recorded performance effects, including
time-dependent effects such as pitch LFO, echo fade and bouncing ball. That
makes a retained origin or phase reference plausible, without assigning a
single effect number. [Official guide](https://teenage.engineering/guides/po-32/en).

Two pairs from the same official bank have identical trigger lanes, parameter
pairs and accent masks, differing only in these sixteen bytes and pattern
index. One pair includes `2B` at step indices 2/3 and `AB` at 10/11; another
contains `8E` at every step from 8 through 15. The high nibble resembles a
shared starting position while the low nibble stays constant.

Across all 896 positions, 226 bytes are nonzero. Among those positions:

| Candidate check | Matches / 226 |
| --- | ---: |
| High nibble indexes a byte with the same low code | 200 |
| Reverse interpretation: low nibble indexes the same high code | 119 |
| Current low code equals step 0's low code | 68 |
| Current low code equals the previous step's low code | 142 |
| High nibble equals the current linear same-code run's start | 154 |

The 26 exceptions reject a mandatory same-code lookup, and the 72 run-start
mismatches reject rebuilding the high nibble solely from current runs. In one
matched pair, step 2 changes `2B` to `2D` while step 3 retains `2B`; its candidate
reference now points at a different code. This is compatible with retained
history after an overwrite, but the actual editing gesture was not observed.
These are correlated corpus positions, not 226 independent hardware trials.

This is a stronger lead than calling the region padding or sixteen unrelated
flags. An effect parameter/phase value correlated with position remains an
alternative to an actual index. The sixteen documented FX plus a clear action
also caution against assuming a trivial four-bit ID map with one permanently
reserved no-effect value. Zero is common, but code-zero behavior and the full
encoding need an isolated mapping. Preserve each entire byte.

### The accent mask looks independently retained

Every active trigger accent is included in the trailing mask in all 56 patterns.
The mask equals the union of active accents in 45; including inactive trigger
accent bits raises exact matches to 47, but is still not a general rule.
Seven patterns have mask bits absent from every lane's accent bits, and three
have a mask bit on a step with no active trigger. Two patterns have inactive
accent bits outside the mask; none has an active accent outside it.

The leading model is a separately retained step-accent track/default with
per-lane event accent copies, potentially updated by different editing paths.
A cache or historical remnant remains possible. This supports preserving the
mask as its own field rather than recomputing an OR of triggers. It does not
establish whether the mask or event bits control playback when they disagree.
The public helper's current edit policy is library behavior, not independent
proof of the device's update rules.

### A field named `flag` may actually carry a pitch control

The first byte of each PO-32 parameter pair is named `flag` in the existing
C API. In these 3,584 per-step pairs it takes many values throughout 0..255;
989 entries are `80`. The implementation also initializes new pairs to `80 01`.
Together with the documented pitch/morph knobs, the same-shaped PO-35
pitch/morph pairs and shared drum patches, this makes **pitch-related scalar
or packed pitch state** a strong candidate for the first byte.

A legacy identifier is not evidence that the byte is Boolean. Conversely,
many byte values do not exclude bit fields, enable bits or sentinel values.
No isolated PO-32 pitch-only edit or acoustic calibration was added here.
Keep `flag` ABI-compatible and raw until its scale and special cases are
established; do not automatically impose PO-35's interpretation.

### Smaller gaps and API limits

- Inactive trigger selectors/accent bits occur 602 times. Last-used selector
  or editor history is plausible, consistent with the other families' retained
  state. Their precise future-note behavior is untested.
- Trigger bit 6 is zero in all 3,584 inspected events. Reserved storage or an
  unused capability are possibilities; this corpus cannot choose between them.
- Low event codes observed are 0,1,2,3,4,8. The code overlap with PO-35 supports
  investigating a related multiplier design, not copying its acoustic rate law.
- One-byte `496F` pattern markers may represent empty-slot declarations or a
  receiver operation. A marked slot can be chained; it is not necessarily an
  omitted slot. Receiver clearing/preservation remains separate from parsing.
- The current typed 16-entry chain capacity and raw parser's length bound are
  library limits, not device limits. The official guide allows 64 chain entries,
  and an official capture already contains 22. This is a known API limitation,
  not an unknown format field. See [PO-32 format limits](PO32.md).

The first useful PO-32 discriminator would isolate one FX edit, then one accent
edit on an otherwise unchanged pattern. Existing captures or controlled
exporter output should be searched before requesting hardware. A separate
pitch-only comparison could challenge the `flag` naming without conflating
the pattern-reference and parameter-pair hypotheses.

## Investigation order

1. Search retained same-other-content comparisons for FX-reference edits and
   key-1/nondefault-multiplier events. Keep actual counterexamples in the results.
2. Use existing root-only header edits to separate metadata from voice-table
   hypotheses; recheck old labels against payload deltas before using them.
3. Once capture is reliable, prefer one short probe that distinguishes competing
   models: one FX-reference change, one key increment, or one sample-BPM edit.
4. Use a broad synthetic range bank for storage/canonicalization coverage only.
   Unchanged readback can mean raw storage, not supported playback. Changed bytes
   can reflect receive/export state updates, not numerical clamping. Playback,
   edit-then-export and untouched readback are separate experiments.

This pass changed documentation only. The new reference-fit audit used archived
frame bytes through the native parser, not fresh waveform recovery. Existing
hardware results remain bounded by their recorded procedures. No additional
hardware interaction is needed to retain these ranked hypotheses.
