# PO-32 packed effect/reference investigation

[PO-32 format](PO32.md) · [Cross-family hypotheses](FIELD_HYPOTHESES.md)

The leading interpretation of pattern bytes **193..208** is one byte per step:
a low-nibble performance-effect code and a high-nibble absolute origin/reference
position. The 2026-11-07 offline follow-up strengthens that orientation, but
does not establish named effect IDs or how playback consumes the reference.
The evidence contradicts a mandatory same-code dereference and reconstruction
solely from the current contiguous effect run.

All offsets and indices below are zero-based decoded payload coordinates.
The final two bytes at 209..210 are the separately stored accent mask.
No hardware tests, application execution or production codec changes were made.

## What Microtonic establishes

The archived Microtonic 3.3.4, build 1048 exporter constructs each pattern as
four interleaved lanes of sixteen triggers and sixteen parameter pairs. It
then appends sixteen literal zero bytes and the accent mask. The transfer
caller prefixes the destination pattern number and passes that payload to
the native modem wrapper.

Read-only inspection of the native wrapper confirms a uniform payload-byte
loop: it whitens/checksums each supplied byte without filling in these sixteen
positions afterward. Thus the zero region is an exporter choice, not an
unexamined placeholder later replaced by hidden FX packing.

This source confirms the layout and a known-zero producer. It supplies no named
FX map, nibble packing or receiver implementation for nonzero bytes. It does
not show that the device ignores the region. Source identities:

| Archived source | SHA-256 |
| --- | --- |
| Original pattern-export script | `5696570066cfdb93f8cc60e1d764dabed57d8dc95751d491445d154635fcff85` |
| Native ARM64 image inspected | `fdaf72d2f8a9fe4d7faefc776c520aa036b7e767db37c6545cfd3a39fa99a814` |

The original script's `serializePattern` performs the zero fill; its
`transferPattern.transmit` prefixes the pattern index. The inspected native
payload loop is at image addresses `0x173e88..0x173f14`. These anchors identify
the archived version, not later releases.

An older research converter also omits this region when producing Microtonic
presets. Converting a backup through that tool and re-exporting cannot validate
FX preservation. Its `reserved_zeros` label came from the exporter behavior,
not an established device constraint. The old parser additionally grouped all
lane triggers before all pairs; that ordering is wrong even though its total
length leaves the sixteen-byte region at the correct offset. Current analysis
uses the native lane-interleaved layout.

## Capture scope and independent checks

The pass combined 61 previously successful source recordings with thirteen
official download entries. These 74 input identities reduce to **61 distinct
frames**, containing 57 distinct full patterns and 28 distinct sixteen-byte
vectors. All archived frame hashes matched. A separate bit-at-a-time CRC and
unwhitening parser agreed with the native parser on every packet tag, offset
and payload, including exact frame termination.

The earlier replay's eight unsuccessful sources remain excluded. They were not
repaired or newly decoded here. Original waveforms were not replayed in this
pass; checks operate on the retained decoded frames.

Of the distinct frames, 48 belong to the older control-reference collection.
They contain **no pattern packets**, so they cannot map these FX bytes despite
their control-oriented labels. Nonzero vectors still come from the three
official full-bank references used in the earlier hypothesis pass. The extra
pattern from the broader archive has a zero vector. A larger frame count is
therefore not additional independent FX evidence.

Two groups of pattern pairs have equal triggers, parameter pairs and accent
masks, differing only in the sixteen-byte region and destination pattern index.
They occur within one official bank. Across complete frames, no pair differs
only in this region with every other packet unchanged. No authenticated
single-FX-button capture was found in the inspected archive.

## Nibble orientation and position models

For a nonzero raw byte `x` at step `s`, test candidate code `c = x & 15` and
reference `r = x >> 4`. The primary score asks whether the byte at reference
`r` has the same candidate code. This is a discriminating correlation, not an
assumption that the device must perform that lookup.

Deduplicating identical vectors avoids counting their occurrences in different
musical patterns as separate evidence. The 28 vectors contain 203 nonzero
positions:

| Model/check | Matching positions / 203 |
| --- | ---: |
| Absolute zero-based `r`, same low code | 177 |
| Absolute `r + 1`, modulo 16 | 152 |
| Absolute `r - 1`, modulo 16 | 20 |
| Relative backward position `s - r` | 81 |
| Relative forward position `s + r` | 84 |
| Reversed nibble roles | 101 |
| Same low code as previous step | 123 |
| Same low code as step 0 | 52 |

Testing all sixteen constant offsets in each relative model raises their best
fits only to 96 backward and 93 forward. Offset zero is the best absolute fit
among the sixteen offsets. These are in-sample comparisons, not held-out
predictions or estimates of semantic correctness.

Removing the constant-code vector leaves 26 varying-code vectors and 187
nonzero positions: the absolute model matches 161, reversed nibbles 85,
previous step 107 and step 0 only 36. The orientation preference is not solely
an artifact of a constant effect across a whole pattern.

The earlier 200/226 result counted distinct full patterns rather than distinct
FX vectors; both populations are valid descriptions of the same corpus.
Stored high nibbles equal the current linear same-code run start at only
147/203 positions after vector deduplication. A universal run-start rule fails.

## Counterexamples and what they permit

There are 26 same-code-reference failures even after vector deduplication:
fourteen have low code 7, nine code 9, and one each codes 5, B and E.
They remain valid, checksum-verified data. Do not repair them to fit the model.

One matched pair is particularly useful. At step 2, raw `2B` becomes `2D`,
while step 3 retains `2B`. Under the candidate interpretation, step 3 still
references position 2 although that position now carries another code.
At step 10, `AB` also becomes `AD`, while step 11's former `AB` becomes zero.
Every other difference is the destination pattern index. This is compatible
with overwriting FX while preserving some earlier origins, but does not prove
which pattern was edited first or authenticate any physical gesture.

Pattern payload hashes for that pair:

- `fed0731edd66d396c378e960b096e83fab36b9d8f24c3b9a144ef98932e2d417`
- `37a340d97a6765ae997d21c61919e00799bbd6e144ced8dc438b46c2b244df0c`

An origin used to compute elapsed phase would not necessarily require the
referenced position to retain its old effect. An effect parameter correlated
with position or retained editor state can also explain parts of the data.
Playback dependence, wraparound and cross-pattern behavior remain unresolved.

No two distinct vectors in this corpus have the same complete low-nibble
sequence but different high-nibble sequences. Thus there is no isolated
origin-only comparison here. PO-35 has examples of that kind, but they are
evidence for a related design, not a substitute PO-32 intervention.

## Effect names and default state remain open

The vectors contain 54 distinct nonzero byte values, with low codes
`2,4,5,6,7,8,9,A,B,C,D,E,F`. Low code 8 occurs with twelve different high
nibbles. This is more compatible with multiple packed properties than a simple
sixteen-value whole-byte FX enum. It still does not independently name either
property.

The [official guide](https://teenage.engineering/guides/po-32/en) documents sixteen
effects and a separate clear gesture. No observed low code can yet be mapped
to a named effect from these captures. In particular, zero is the original
exporter's default, but this does not prove a receive-time clear action or that
zero can never encode an effect in another state. An exact named mapping needs
an isolated control or a device implementation read path.

## Consequence and next useful evidence

Keep the bytes raw and preserve them during unrelated edits. Do not generate
high nibbles from run boundaries, zero nonmatching references, or use Microtonic
export as an FX-preservation oracle. Existing public struct names and behavior
remain unchanged.

The next useful evidence is narrow: a known effect at a nonzero step, followed
by an edit of its origin while a later affected step remains unchanged. That
would discriminate effect numbering and retained-origin behavior. A local
device implementation would be stronger still. Neither is present in the
inspected source/capture set, so this pass does not request another hardware
test or promote the hypothesis into a renderer.
