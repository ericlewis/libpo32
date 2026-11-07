# PO-35 multiplier edits and the extra step-3 byte

[Drum-event evidence](PO35_DRUM_EVENTS.md) · [PO-35/137 format](PO35.md)

The extra `80` at step 3 in the original multiplier test is **not required by
multiplier encoding**. A clean repeat from a byte-identical serialized baseline
changed only the selected event's low nibble. Its original cause remains
unresolved; incidental input or transient editor state are possibilities, not
established explanations.

## Existing recordings checked first

Both original WAVs were demodulated again and reproduced the retained frames
exactly. A separate bit-at-a-time checksum/unwhitening implementation validated
all captured packets and the terminal tail. The original changes at pattern
bytes 97 (`81 → 82`) and 99 (`00 → 80`) are therefore reproducible decoded
evidence, not a disagreement between the two packet parsers or a repair artifact.

The 100 archived capture identities were revalidated, retaining 62 distinct
patterns. They do not establish a universal endpoint-marker rule. For nonzero
drum selectors, 377 code-1 events have a next cell inside the pattern: 45 have
a matching selector with zero event code, 137 have a different selector with
zero code, and 195 have a nonzero-code destination.

The old corpus's two code-2 events and one non-wrapping code-3 event have active
cells at the proposed `start + code` positions. The remaining code-3 event
crosses the pattern boundary; assuming wraparound lands on a zero-code cell
with a different selector. Those final snapshots cannot identify an
edit-specific write that may have been overwritten later.

The earlier one-step/all-step control pair uses selector zero, whose zero-code
marker would be indistinguishable from cleared storage. It does not resolve
the extra high nibble seen with drum key 9.

## Prediction and hardware results

Before the follow-up, the tested hypothesis was that multiplier edits write a
zero-code byte carrying the drum selector at `start step + new multiplier`.
For step 1 and multiplier 2, this would explain physical step 3. The frozen
prediction for a 2 → 3 change was a new `80` at step 4.

On 2026-11-07, the user kept Pattern 16 stopped with drum key 9 on step 1,
held that step and tapped BPM once, confirming a displayed value of 3. Only
pattern byte 97 changed `82 → 83`. Step 3 stayed `80`; step 4 stayed `00`.
**The moving-marker prediction failed.**

For a clean repeat, the test pattern was cleared and recreated. An initial
combined clear/rebuild attempt did not produce a fresh baseline and was
excluded from the repeat. A separate clear operation was read back as the
empty-pattern marker. After recreation, the accepted baseline matched the
original drum-key-9 frame byte for byte, including every transmitted pattern,
settings, table and patch payload.

The user again held step 1 and tapped BPM once, reporting displayed multiplier
2. This time **only byte 97 changed `81 → 82`**; step 3 remained zero.

| Operation | First four drum-event bytes before | After |
| --- | --- | --- |
| Original 1 → 2 test | `81 00 00 00` | `82 00 80 00` |
| Subsequent 2 → 3 test | `82 00 80 00` | `83 00 80 00` |
| Clean repeat of 1 → 2 | `81 00 00 00` | `82 00 00 00` |

The original and repeated multiplier-2 outcomes differ only at byte 99,
corresponding to step 3. The pitch/morph pair remains `80 01`, the stored mask
remains zero, and settings byte 154 remains 23 throughout these comparisons.

## Provenance and limits

Accepted readbacks are complete 50-packet selective frames for pattern index
15. Native parsing and independent checksum validation agree. No frames were
repaired or stitched; failed and timed-out recordings were excluded. USB
timestamp discontinuities prevent acoustic timing claims for the recordings.
No rate sweep or playback-duration measurement was performed.

Frame SHA-256 values:

| State | Hash |
| --- | --- |
| Original and recreated baseline | `92d55e803375ee54ed89dade0b2476ffb31ddbe11b5dd3435f46808c7d402680` |
| Original multiplier 2 | `f64107ceaaf29948a2409e1a8ac65800a5828f3b264a5d12ddb40ff5af932366` |
| Multiplier 3 | `b0f1ff2b8d4d91302cd0b747eaacc86d37d0c339604efd58d04c06cdbf322259` |
| Clean-repeat multiplier 2 | `c99976452a7dacfdedcb8b5264b8be9378d83f031651d9e3b7941df7f7e985c1` |

Identical serialized baselines do not establish identical transient UI state
or identical physical gestures. Other patterns and speech were omitted by
selective SEND and were not compared. The original step-3 write cannot be
attributed to an accidental press or a specific firmware routine from these
results alone.

## Consequence for editing

Preserve zero-low-code bytes when they exist, but do not synthesize a step-3
marker or a general endpoint marker from the multiplier value. The tested
1 → 2 and 2 → 3 edits can change just the selected event byte. Displayed values
2 and 3 correspond to low codes 2 and 3 in these observations; this does not
calibrate every possible code or its audible timing. Codec behavior is unchanged.
