# PO-35 control-range hardware roundtrip

[PO-35/137 format](PO35.md) · [Field hypotheses](FIELD_HYPOTHESES.md)

A PO-35 Speak accepted a generated control-range bank on 2026-11-07. Its
readback preserved **all 1,536 per-step parameter bytes**, covering every raw
value 0..255 once in each of six control planes. Only six global parameter
bytes changed, each from zero to one. The original bank was then restored and
read back byte-identically.

This is storage and transfer evidence on PO-35, not a calibration of audible
parameter ranges or a PO-137 hardware result. No intentional playback test
was performed, and the interaction history has the qualification below.

## Probe contents

The probe used a newly captured full bank as its source. All drum patches,
indexed tables, speech headers and speech-body packets were retained. Sixteen
patterns were replaced, and global parameter edges, tempo, raw swing and chain
were edited. Counts and references remained structurally valid; this was not a
capacity or malformed-length test.

Each of the four speech parameter planes and the drum pitch/morph planes
contained values 0..255 across the sixteen patterns' 256 steps. The probe also
used drum low codes 1,2,3,4,6,8, voice/effect index nibbles 0..7, tempo 240, raw
swing byte 18 and a 64-entry chain. Both probe and restore waveforms passed
exact software modem loopback before transmission. The operator reported End
after receiving the probe.

## Exact readback differences

Sent and returned frames each contain 579 packets and 95 reassembled logical
records. Ninety-four records are identical. Only the 220-byte settings record
differs at these zero-based offsets:

| Offset | Global control cell | Sent | Returned |
| ---: | --- | ---: | ---: |
| 0 | Speech slot 0, parameter 0 (pitch) | 0 | 1 |
| 11 | Speech slot 1, parameter 3 | 0 | 1 |
| 50 | Speech slot 6, parameter 2 | 0 | 1 |
| 89 | Speech slot 11, parameter 1 (formant) | 0 | 1 |
| 120 | Drum key index 0, pitch | 0 | 1 |
| 151 | Drum key index 15, morph | 0 | 1 |

These are exactly all six zero-valued cells among the 92 global parameter
cells. Every other settings byte was preserved, including all 64 chain entries,
tempo, raw swing and retained state. Every pattern byte was preserved: scalar
parameters, events, index pairs, references, footers and masks. Sound data,
speech metadata, tables and drum patches were unchanged.

The observations support a global zero-normalization or minimum-value
interpretation. They do not locate the change at receive, SEND or another
intervening state transition. Pattern zeros remaining intact are compatible
with a no-update/inherit sentinel, but do not prove that meaning. Values which
survive readback need not have distinct or valid audible effects. In particular,
preserved event codes and a 64-entry chain do not calibrate multiplier timing
or establish playback of the whole chain.

Raw codecs continue preserving zero and every other byte. They must not apply
this observed device transformation automatically or reject captured zeros.
Generated PCM speech was not tested: the speech bodies were copied unchanged.

## Interaction and recording limits

During readback the operator reported possible button mishandling. The recording
contains an incomplete first transfer and a complete second transfer. The
original decoder stopped at the first candidate; a sample-preserving crop
exposed the second. Cropped PCM was checked byte-identical to the source suffix.
No protocol bytes were repaired, reconstructed or stitched between attempts.
The updated capture decoder also recovers the identical frame directly from
the untrimmed recording by validating later transfer candidates.

This is a qualified receive-to-readback observation, not an isolated test of
receive-time normalization. The six distributed changes are verified bytes;
the uncertain gesture does not establish their cause.

TX-6 capture at 48 kHz passed a 60-second continuity check. The original-bank
and test-readback recordings also had zero timestamp resets, queue drops or
overloads, with no clipping. The restore recording had one startup reset before
the modem burst; its full transfer still validated and matched the original
114,154-byte frame exactly. Do not describe that whole recording as timing-clean
or infer a permanent fix for the earlier USB capture failures.

## Evidence identity and verification

| Frame | SHA-256 |
| --- | --- |
| Original bank and restored readback | `e31e0ff7cae641aecf5d74ecbd8e57a4165cdbfd136b174008b86d73ccb82a7c` |
| Synthetic probe | `eaa57bdb68ab0b264fd88cc74d1db37d73028d46f4c3abd29b6e09f9f460db63` |
| Probe readback | `01d2efe325315aebb50e63073ce9cc1f8281adf95e9f4516da47ba8db1ad60c1` |

Validation checked complete native parsing, logical records with reassembled
speech bodies, all six scalar-plane mappings, exact changed settings offsets
and original-bank restoration. Comparator controls included identical input
(no differences) and the known source-to-probe edit (sixteen patterns and
settings only). Private recordings and analysis artifacts remain outside the
repository; no personal paths or capture filenames are needed to identify the
bounded results.
