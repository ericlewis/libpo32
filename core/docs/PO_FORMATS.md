# Pocket Operator backup formats

These layouts describe the records handled by the C codecs. Fields marked raw
must survive edits unchanged unless explicitly selected. Model numbers identify
compatible formats; the data does not authenticate the device that produced it.

**Structural** descriptions below are verified record layouts and byte
relationships. **Observed roles** have controlled capture or playback support,
but may lack a complete numeric scale. **Candidates** remain unconfirmed.
Keeping a field raw in the API does not mean nothing is known about it: it
preserves values that the semantic model cannot yet reproduce safely.

## Family references

| Devices | Records and evidence |
| --- | --- |
| PO-32 | [Pattern data and preservation limits](formats/PO32.md) · [Packet protocol](PROTOCOL.md) |
| PO-33 / PO-133 | [Settings, patterns and samples](formats/PO33.md) · [Effects and retained state](formats/PO33_EFFECTS_STATE.md) |
| PO-35 / PO-137 | [Tables, patterns and speech](formats/PO35.md) |

Use the [C API guide](C_API.md) for function contracts and the
[command-line guides](../examples/README.md) for file workflows.

## Current understanding

See the [cross-family analysis](formats/FAMILY_RELATIONSHIPS.md) for measured
relationships between framing, drum patches, settings and pattern layouts,
including the new sound-only receiver evidence.

Transport and record layout are better understood than every field's playback
meaning. Tested banks can be decoded and rebuilt while retaining unknown bytes;
that does not require assigning those bytes a musical role.

| Family | Established capabilities | Main remaining gaps |
| --- | --- | --- |
| PO-32 | Mono modem, drum patch parameters, synthesis and pattern editing | Some per-step/control bits and slot-marker receive behavior; typed chain/trigger preservation limits |
| PO-33 / PO-133 | Stereo modem, lossless records, sample codec/resampling and shared-pool topology; PO-33 replacement/growth, melodic insertion and copied-sound detachment readbacks/playback | Exact range endpoints and slice mapping, shared-owner relocation, full note/timing/parameter scales, most FX codes and retained-state update rules, maximum storage capacity |
| PO-35 / PO-137 | Stereo modem, lossless records, speech-group replacement and approximate PCM speech analysis/rendering; shared PO-32 drum patch codec; PO-35 root and normal pitch/formant storage observations | Indexed voice/effect tables, exact speech gain/noise and synthesis behavior, root acoustic scale and other header metadata, key/scale and other pattern/control semantics; generated speech hardware acceptance |

PO-133 and PO-137 share the inspected 33/35 record grammars, respectively;
that does not establish identical firmware, audio or physical cross-loading.
The family references above distinguish verified layouts, observed roles and
candidate interpretations. There is no meaningful single completion percentage
across framing, byte preservation, musical semantics and device validation.

## Reading the layouts

A **packet** is a tagged, length-delimited unit in a transfer. Its **payload**
is the data carried inside it. A **logical record** is the data for an item
such as a pattern or sound; it can span several packets. A **selector** identifies
the slot or section that subsequent data belongs to.

Offsets are byte positions, starting at zero where specified. A **nibble** is
four bits, or half a byte; `LE32` means a four-byte little-endian value. The
[architecture guide](ARCHITECTURE.md#byte-domains-and-ownership) explains how
packet payloads, encoded bodies and complete frames differ.

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

PO-32 state (`505A`) and PO-35 settings (`84D8`) each contain sixteen two-byte
drum-control pairs. Their first bytes have different stored roles: a raw flag
in PO-32 and pitch control in PO-35. The observed `80 80` pairs in one PO-32
pack and `80 01` pairs in a PO-35 reference also differ. The shared PO-32 drum
patch codec does not make these settings records interchangeable.

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

## Limits

The PO-33/35 structural codecs preserve record bytes. The
[PO-32 typed limits](formats/PO32.md) require raw packets for exact preservation
in some cases. Observed field
roles do not establish complete numeric laws. Selected rebuilt PO-33 backups
and PCM sample replacements have user-confirmed device acceptance, and
selected test banks have byte-identical device readbacks. The integrated editor's
[tested replacements](formats/PO33.md#sample-replacement-hardware-validation)
include growth from 45,056 to 49,152 sample bytes. Those observations do not
validate every input, other growth sizes, maximum capacity, PO-133 hardware,
or generated PO-35/137 speech. Software roundtrips and physical acceptance
remain separate evidence.
