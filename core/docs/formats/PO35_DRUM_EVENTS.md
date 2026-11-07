# PO-35/137 drum events: evidence and remaining mappings

[PO-35/137 format](PO35.md) · [Cross-family comparison](FAMILY_RELATIONSHIPS.md)

The strongest working interpretation is a **four-bit drum selector in the high
nibble and an event/multiplier code in the low nibble**. The record layout and
several exact byte relationships are established. A subsequent PO-35 readback
test maps drum keys 1 and 9 to high nibbles 0 and 8. Follow-up readbacks map
displayed multipliers 2 and 3 to low codes 2 and 3. The complete key/rate map
and audible timing remain uncalibrated.

The initial 2026-11-06 investigation used existing captures only. Its corpus
counts below remain separate from the later hardware follow-up in this guide.
Neither pass changed codec behavior.

## Offline scope and validation

All 100 retained PO-35/137 capture identities matched their archived frame
hashes and passed current native framing/checksum validation with exact complete
consumption. Known pattern and settings records also passed their typed
decoders. The counting populations are:

| Population | Full `CCB2` occurrences | Empty `29FF` markers | Distinct full payloads |
| --- | ---: | ---: | ---: |
| All 100 capture identities, including repeated frames | 85 | 884 | 62 |
| After deduplication to 89 distinct frames | 82 | 726 | 62 |

The earlier guide's 85 full records uses the first population; this pass's
82 occurrences uses the second. Of the 47 distinct frames with sixteen pattern
records, only three contain sixteen full `CCB2` patterns. Eleven contain one
full pattern plus fifteen empty markers, and 33 contain sixteen empty markers.
Thus sixteen pattern records does not mean sixteen full patterns.

Removing the pattern-index byte still leaves 62 distinct full payloads; their
drum-event regions contain 50 distinct vectors. Nibble statistics below use
those 62 payloads, not all repeated occurrences.

Six more recent PO-35 readbacks also passed native parsing. They contain only
empty-pattern markers and add no drum-event observations. These are distinct
from a full pattern whose event bytes happen to be zero.

The comparison set is twelve distinct official PO-32 frames containing 56 full
patterns. Original waveform labels guide searches but do not authenticate the
button gestures. Exploratory tools and private source-path mappings remain
outside the repository.

## Exact layout and census

Within a 183-byte `CCB2` pattern, step `s` is zero-based:

| Data | Offset |
| --- | ---: |
| Drum event byte | `97 + s` |
| Drum pitch/morph pair | `113 + 2*s` |
| Stored mask, LE16 | `181..182` |

The 992 drum-event bytes across distinct patterns have these low nibbles:

| Low nibble | Occurrences |
| ---: | ---: |
| 0 | 510 |
| 1 | 478 |
| 2 | 2 |
| 3 | 2 |
| 4..15 | 0 |

Every high nibble from 0 through 15 occurs with a nonzero low nibble. Bit 6 is
set in 202 bytes and bit 7 in 278. Among the zero-low-nibble entries, 132 retain
nonzero high bits. Do not normalize an inactive-looking entry to zero: stored
selection/history can remain there.

The [manufacturer's guide](https://teenage.engineering/guides/po-35/en)
identifies Sound 16 as the Tonic section and documents a step multiplier.
That makes a selector-plus-multiplier interpretation plausible; it does not
publish the binary numbering or numeric rate mapping.

## A clean drum-pattern change

An exact same-other-content search found a pair with the same packet order,
lengths and every payload outside one pattern. Within zero-based packet 48 (`CCB2`, pattern
index 0), exactly 45 bytes change:

| Region | Before | After |
| --- | --- | --- |
| Event at step 0 | `01` | `01` |
| Events at steps 1..15 | `00` | `01` |
| Parameter pair at step 0 | `01 01` | `01 01` |
| Parameter pairs at steps 1..15 | `00 00` | `01 01` |
| Stored final mask | `8000` | `8000` |

The retained labels describe one step versus all sixteen steps in the drum
section. The bytes independently establish replication of the same event and
paired controls while every other packet remains identical. This supports
`01` as a normal populated drum step, without treating the filename as proof
of which individual drum key produced it.

Frame hashes:

- Before: `75454c5b73f435948096ca5c44c8f28a4868e9f08cb106340070056262c6c115`
- After: `fe862bf165050d5693411b1bd32a3acc2d0c6c336c496b6cb563eca8623f3092`

The earlier comparison limited near pairs to 24 changed bytes; this 45-byte
pair was outside that search. The new search masks the whole drum region and
has no byte-difference cutoff. It finds no pair differing only in drum-event
bytes, and only this one pair differing within drum events, paired controls,
and the final mask. Thus it supplies no isolated high-nibble or multiplier edit.

## Reconfirmed final-mask relationship

The existing [format guide](PO35.md#pattern-records) already records this mask
relationship; it is reconfirmed here, not newly discovered. For every one of
the 62 distinct patterns, the following arithmetic reproduces the stored mask.
It was checked independently in native C using the typed
pattern fields and in the analysis script using raw byte offsets:

```text
M = 0 (at least 32 bits)
for each speech event S whose low nibble is nonzero:
    M |= 1 << (S >> 4)
for each drum event D whose low nibble is nonzero:
    M |= 1 << (15 + (D >> 4))
stored mask = M & 0xFFFF
```

Consequently, in this corpus, the drum contribution survives only when its
high nibble is zero. Simply adding bit 15 whenever any drum event has a nonzero
low nibble matches only 36 of 62 patterns. Treating the mask as a bitmap of
active drum steps matches none; a bitmap of drum-event bit-7 positions matches
only one. The same naive rule matches 59 of 85 full-pattern occurrences before
frame deduplication, or 56 of 82 afterward. These are different denominators
for the same retained data, not contradictory measurements.

A concrete counterexample is the public PO-137 reference's pattern 5: it has
three `61` drum events, no active speech events, and a stored mask of zero.
Therefore a zero mask does not establish an empty or silent pattern.

This is a corpus-perfect relationship, **not a recovered firmware algorithm**.
It resembles truncating a wider reference bitset, but does not prove a bug or
explain what consumes the field. In these data, testing for raw drum byte `01`
produces the same result: no high-zero events with low code 2..15 are available
to distinguish that alternative. Keep `derived_mask` stored verbatim; do not
add automatic recomputation from this fit alone.

## What PO-32 does and does not explain

PO-35's sixteen event bytes followed by sixteen two-byte parameter pairs have
the same 48-byte shape as a PO-32 lane. But PO-32 places instrument-group bits
at 4..5 within one of four lanes, leaves bit 6 unassigned, and uses bit 7 for
accent. Its 3,584 event bytes in this comparison never set bit 6, whereas the
PO-35/137 corpus uses all sixteen high-nibble values.

The two ending masks also behave differently. A used-instrument bitset fits
zero of the 56 PO-32 masks. Active-trigger accent positions fit 45; including
bit 7 from inactive entries fits 47. Neither fits all, consistent with retaining
the stored mask instead of rebuilding it from trigger accents. The PO-35
reference-related fit must not be applied to PO-32's ending field.

PO-32 low codes observed here are 0, 1, 2, 3, 4 and 8. Their overlap with PO-35
does not prove an identical multiplier law, and the common byte geometry does
not justify reusing PO-32's event decoder for the speech family's drum plane.

## Rare codes and misleading controls

All four low-code-2/3 observations occur in the public PO-137 reference:

| Pattern | Step | Raw event |
| ---: | ---: | --- |
| 6 | 13 | `A2` |
| 12 | 3 | `A2` |
| 12 | 11 | `A3` |
| 14 | 15 | `E3` |

Pattern and step numbers in this table are one-based. Their paired parameter
bytes differ too, so these are not controlled comparisons of multiplier codes.
The data cannot select between repeat counts, encoded rates, or another event
code interpretation, nor establish behavior for unseen codes 4..15.

The older imported-Tonic-pattern and per-drum-knob captures are useful negative
controls: the relevant banks contain empty-pattern markers, not full drum
patterns. They cannot anchor a drum-event mapping. A label mentioning drum 16
also cannot distinguish selecting the Tonic section from selecting its sixteenth
drum key. One such capture has raw event `01`; its step parameter pair differs
from the current sixteenth drum's settings, so the label is not a clean index
intervention.

An additional previously failed six-channel recording was checked for hidden
signal on other channels: channels 3..6 are digital zero, and channels 1..2
have roughly -94.5 dBFS RMS with peaks below -79.9 dBFS. It provides no usable
modem evidence and is not interpreted as a pattern containing zero events.

## Hardware follow-up: drum key and multiplier

A PO-35 Speak was tested while stopped, with both knobs untouched. A selective
readback first confirmed Pattern 16 was empty. The user selected the Tonic
section (Sound 16), auditioned drum key 1 with WRITE off, then entered WRITE
mode and placed step 1. The following changes were made separately, with a
readback after each:

| State | Step 1 event | Step 1 pitch/morph | Stored mask | Settings byte 154 |
| --- | --- | --- | --- | ---: |
| Drum key 1, ordinary step entry | `01` | `80 01` | `8000` | 15 |
| Drum key 9, same step replaced | `81` | `80 01` | `0000` | 23 |
| Drum key 9, displayed multiplier 2 | `82` | `80 01` | `0000` | 23 |

Each accepted readback is a complete 50-packet selective frame with one full
pattern, settings, drum patches and indexed tables, and no speech groups.
The target is pattern index 15, step index 0. The native parser validates all
packets and the terminal tail. No source bytes were repaired, reconstructed,
or combined between takes.

For the key change, the user left WRITE mode, tapped drum key 9, returned to
WRITE, and toggled step 1 off then on. Exactly three transmitted payload bytes
changed:

- Pattern byte 97: `01 → 81`.
- Pattern byte 182: `80 → 00`, changing the LE16 mask from `8000` to `0000`.
- Settings byte 154: `0F → 17` (15 → 23).

This directly associates the event's high-nibble change `0 → 8` with physical
drum keys `1 → 9`, rather than a PO-32-style accent change. The zero mask is
stored alongside an explicitly entered drum-key-9 event, consistent with the
earlier reference-bitset observation. The settings change is consistent with
`15 + drum_index` in a combined speech/drum identifier space for these two
points; selected versus last-played state and the remaining IDs are unresolved.

For the multiplier change, the user held the lit step-1 button and tapped BPM
once, then reported a displayed value of 2. Exactly two pattern payload bytes
changed, with all other transmitted payloads identical:

- Byte 97: `81 → 82` at step 1.
- Byte 99: `00 → 80` at step 3, retaining a zero low event code.

The first change ties low code 2 to the reported multiplier setting while
preserving the selected drum and pitch/morph pair. The second is a recorded
cochange, not evidence of a new triggered step or a proven automatic-fill rule.
The experiment is not described as a single-byte edit. It provides another
reason to preserve the high bits of zero-low-code entries. No playback-timing
measurement or full multiplier-cycle sweep was performed.

A [later clean repeat](PO35_MULTIPLIER_STATE.md) began with a byte-identical
serialized baseline and changed only `81 → 82`, leaving step 3 zero. A separate
`82 → 83` edit retained the old step-3 value without writing a new marker at
step 4. The original cochange is not required by multiplier encoding; its
specific cause remains unresolved.

Readback frame hashes, in table order:

- `1f70548986c7fc289a1a430eb94eaf93fe7408085b13bde58f8fc48fed7ee267`
- `92d55e803375ee54ed89dade0b2476ffb31ddbe11b5dd3435f46808c7d402680`
- `f64107ceaaf29948a2409e1a8ac65800a5828f3b264a5d12ddb40ff5af932366`

The USB recordings had timestamp discontinuities overall. Two key-change
takes failed complete-frame recovery and were excluded; cropping them did not
produce a valid frame either. A fresh repeat provided the accepted key-change
frame. These successful native checks establish usable decoded payloads, not
clean timing for the entire recordings. Other patterns and speech were omitted
by selective SEND and were not included in the byte comparisons.

## Decision

- Keep the four-bit selector plus low event-code interpretation as the leading
  model. Bits 6 and 7 cannot simply inherit PO-32's treatment.
- Preserve zero-low-nibble bytes and the final mask exactly.
- Retain the observed mappings for physical keys 1 and 9 and displayed
  multipliers 2 and 3. Do not promote these points into a verified map of every key,
  unused low code, or acoustic multiplier behavior.
- Preserve the extra zero-low-code entry when present, but do not generate it
  from a multiplier value: the clean repeat does not require that write.
  No additional hardware action is needed to retain these bounded findings.
