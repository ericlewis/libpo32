# PO-35/137 drum events: evidence and remaining mappings

[PO-35/137 format](PO35.md) · [Cross-family comparison](FAMILY_RELATIONSHIPS.md)

The strongest working interpretation is a **four-bit drum selector in the high
nibble and an event/multiplier code in the low nibble**. The record layout and
several exact byte relationships are established; the physical drum-key mapping
and audible meaning of codes 2 and 3 are not independently calibrated.

This 2026-11-06 investigation used existing captures only. It did not perform
hardware gestures, generate a new device transfer, or change codec behavior.

## Scope and validation

All 100 retained PO-35/137 capture identities matched their archived frame
hashes and passed current native framing/checksum validation with exact complete
consumption. Known pattern and settings records also passed their typed
decoders. Deduplication leaves 89 frames, 82 full-pattern occurrences, and 62
distinct full-pattern payloads. Removing the pattern-index byte still leaves
62 distinct payloads; their drum-event regions contain 50 distinct vectors.

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

## The final mask has a precise, unusual relationship

For every one of the 62 distinct patterns, the following arithmetic reproduces
the stored mask. It was checked independently in native C using the typed
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
only one.

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

## Decision

- Keep the four-bit selector plus low event-code interpretation as the leading
  model. Bits 6 and 7 cannot simply inherit PO-32's treatment.
- Preserve zero-low-nibble bytes and the final mask exactly.
- Do not assign physical drum numbers or numeric multiplier behavior solely
  from labels or these factory patterns.
- No new hardware test is needed to retain these findings. If exact numeric
  mapping becomes necessary, a controlled drum-key change on one fixed step
  and a separate multiplier change would distinguish the remaining candidates.
  The existing corpus has no such isolated pair.
