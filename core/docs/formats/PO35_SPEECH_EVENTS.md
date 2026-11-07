# PO-35 / PO-137 speech events

[Record layout](PO35.md) · [Typed C helpers](../api/PO35.md#speech-events) ·
[Unresolved models](FIELD_HYPOTHESES.md)

The shared format stores a speech event byte and a secondary byte for each
step. Controlled PO-35 playback identifies their event, note and gate roles.
The byte views also apply to PO-137 records; these playback experiments did
not independently test a PO-137 or establish identical voice behavior.

## Fields and known event codes

Offsets are within a decoded 183-byte pattern payload; step `s` is zero-based.

| Field | Location | Established role or limit |
| --- | --- | --- |
| Slot | High nibble of `1+s` | Selects a speech slot; 0..14 are known slots |
| Event code | Low nibble of `1+s` | `1` starts the source again; `F` continues an ongoing voice |
| Note code | High nibble of `17+s` | Selects the played note in the pattern's key/scale context |
| Gate code | Low nibble of `17+s` | Controls note duration; exact cutoff and rounding remain unresolved |

Event code `0` marks an inactive event, but its slot and secondary bytes can
retain state. Code `2` occurs in archived patterns and remains unresolved.
Slot `15` has no assigned speech-slot meaning. The raw codecs and typed views
preserve all these values rather than converting them to ordinary onsets or
clearing them. A zero gate code is not an inactivity test: it produced an
audible short burst in the controlled sweep.

## Onset, continuation and note changes

A test source with pitch changes at known record positions distinguishes an
advancing sound from one repeatedly restarted at its beginning. Matched
patterns change only the relevant event or secondary bytes, apart from their
pattern indices.

- Code `1` restarts the source. Repeated onsets return to the initial marker
  segment, while a matched onset followed by `F` events advances through it.
- Code `F` preserves source position across a pattern boundary. It does not
  implicitly restart merely because the next pattern begins.
- Changing the note code on `F` changes audible pitch while the source keeps
  advancing. Returning to the earlier note matches the original source
  trajectory, not a restarted one.
- Changing the first gate from `F` to `7` ends the initial note. Later `F`
  events remain at the input noise floor rather than starting it again.
  This tests gate `7` against `F`; it does not test every shorter code or
  every possible continuation context.

Across three complete passes and both channels, source-position comparisons
match the continuing/reset reference appropriate to each case within a median
0.31 cents. Competing references differ by roughly 291–300 cents. The
short-gate continuation is more than 41 dB below its full-gate reference,
with no accepted periodic output in the later steps. Channels and neighboring
measurement windows are correlated; there were three physical repetitions.

These observations strengthen the earlier corpus result: all 367 linear `F`
events examined follow an active event with a full secondary low nibble.
Stored notes, controls and voice/effect indices can nevertheless change across
that boundary. One archived continuation changes speech slot too; its source
position and retrigger behavior were not isolated by this playback test.

## Note and gate sweeps

All sixteen note codes were measured with explicit pitch/formant `128`,
start `1`, speed `128`, index pair `70` and footer `00 77 00 00`. The observed
relative intervals, rounded to semitones, are:

| Note codes | Relative intervals from code 0 |
| --- | --- |
| 0..7 | 0, 2, 4, 5, 7, 9, 10, 11 |
| 8..15 | 12, 14, 16, 17, 19, 21, 22, 23 |

Codes eight apart differ by an octave within 2.56 cents in this context.
The rounded table is not an exact tuning law: deviations from its semitone
values reach about 10 cents. Predeclared major/minor scale candidates fit the
four calibration notes but failed held-out notes, so neither is assigned as
the device's scale. Other key/scale contexts still need their own mapping.

Every gate code from `0` through `F` produces an ordered increase in envelope
duration. Measurements favor approximately one sixteenth of a sequencer step
per code increment over a one-fifteenth-step model. Attack, release and
quantization prevent an exact conversion to seconds or ticks. The
[timing hypotheses](FIELD_HYPOTHESES.md#8-gate-timing-and-source-clock)
remain separate from these observed field roles.

The continuation note change also exposes a numeric limit: the measured
interval differs by about 14 cents from the flat-tone note ratio's prediction.
Note updates and source continuity are supported; a universal multiplicative
pitch formula is not.

## Parameter zero is event-dependent

Separate matched-control tests distinguish parameter zeros from note/gate
codes. These are the pitch/formant bytes at `33+4*s` and `34+4*s`:

| Parameter and context | Observed behavior |
| --- | --- |
| Pitch zero on code `1` | Minimum-like starting response, rather than the saved sound default |
| Pitch zero on code `F` | Retains the previous pitch setting while a formant change can still apply |
| Formant zero on code `F` | Retains the previous formant setting while a pitch change can still apply |

Those control tests had byte-identical pre-play and post-play readbacks.
Retaining a control does not imply constant output throughout the step:
interpolation toward a later nonzero value can still occur. The observations
reject a generic whole-row skip when either of these controls is zero. They
do not assign start/speed zero, all-zero-row implementation details, other
voices or unnamed event codes. Preserve raw zeros; do not globally replace
them with `1` or interpret all zero fields as inheritance.

## Evidence boundary and API behavior

The event test used a native tone and a pitch-coded derivative, retaining the
other speech-record fields and packet boundaries. The PO-35 accepted the
transfer, and full readbacks before and after playback matched all 151,138
bytes and 730 packets. The three analyzed passes had no recording timing gaps
or digital clipping. The original bank was restored and read back exactly.
This establishes the tested storage and playback behavior, not arbitrary
input validity, exact device synthesis or PO-137 hardware equivalence.

`po35_speech_event_t` and its encode/decode helpers expose only the four raw
nibbles. They retain inactive and unnamed values, leave the existing pattern
layout intact, and never infer a note frequency, gate time, reference mask or
playback state. See the [API contract](../api/PO35.md#speech-events).
