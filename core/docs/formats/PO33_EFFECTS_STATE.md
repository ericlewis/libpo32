# PO-33 effects and retained state

[Format overview](../PO_FORMATS.md) · [Documentation](../README.md)

See the [pattern layout](PO33.md#record-layout) for the packed FX bytes and
[settings layout](PO33.md#record-layout) for the sixteen retained state bytes.
These observations constrain their interpretation; they do not establish a
complete sequencer model.

## Pattern effects and hardware observations

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
[high-first storage order](PO33.md#record-layout); short-event transients still
differ from sustained FX.

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

## Early grids and possible display state

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
a general update rule, wrap behavior, or whether the value denotes a current or
next position under
the device's indexing convention. Preserve it when editing unrelated fields.

A later stopped-selection/export comparison used that unchanged chained bank.
With WRITE reported off and no standalone PLAY requested, selecting one-based
Pattern 2 and sending a backup changed only the active chain (sixteen entries
to Pattern 2 alone) and settings byte 387 from 0 to 2. Every pattern, footer
and sample record matched. Selecting Pattern 16 and sending again then changed
only the chain selection; the final state coordinate at byte 401 remained 7.
Both complete readbacks validated 423 packets. These observations associate an
update with the manual selection/chain replacement/export path without requiring
new requested playback. The proposed endpoint values 0 or 16 were not written
to the final coordinate in the second comparison. Chain-length change, initial
state, boundary position and prior runtime history differ between the two cases;
selection and SEND are also not isolated. No general update gate follows yet.
The stopped/WRITE-off condition is user-reported rather than independently
observed from the audio.

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
