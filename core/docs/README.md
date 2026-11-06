# Documentation

Choose the kind of work first, then your device family.

| Start here | What it covers |
| --- | --- |
| [Command-line tools](../examples/README.md) | Build the tools, prepare captures and verify transfers |
| [C API](C_API.md) | Install the library, manage buffers and check completion |
| [Backup formats](PO_FORMATS.md) | Shared transport, byte notation and validation scope |
| [Architecture](ARCHITECTURE.md) | Core modules, host responsibilities and package compatibility |

## By device family

| Devices | WAV workflows | C API | Record layouts |
| --- | --- | --- | --- |
| PO-32 | [Examples and capture input](../examples/README.md#build) | [Packets, modem and drums](api/PO32.md) | [Preservation limits](formats/PO32.md) · [Protocol](PROTOCOL.md) |
| PO-33 / PO-133 | [Samples and patterns](guides/PO33.md) | [Records and PCM conversion](api/PO33.md) | [Settings, patterns and samples](formats/PO33.md) |
| PO-35 / PO-137 | [Speech replacement and previews](guides/PO35.md) · [transfer shapes](../examples/README.md#po-35-transfer-shape-manifest) | [Records and speech audio](api/PO35.md) | [Tables, patterns and speech](formats/PO35.md) |

The stereo families share [frame and modem APIs](api/STEREO.md). PO-33's
[effects and retained-state evidence](formats/PO33_EFFECTS_STATE.md) is separate
from its sample and pattern layout. PO-35/137 drum patches reuse the PO-32
patch codec and synth; see the [signal path](SYNTH.md) and
[patch parameter reference](PATCH_PARAMETERS.md).

## Working on libpo

[Contributing](../../CONTRIBUTING.md) covers verification and code conventions.
[Fuzzing](../fuzz/README.md) describes the fuzz harnesses.
[Releasing](../../RELEASING.md) and the [changelog](../../CHANGELOG.md) cover releases.

Public headers define function contracts. The format guides distinguish verified
structure, observed roles and unresolved candidates. Software roundtrips,
physical device acceptance and synthesized-audio fidelity are separate claims.
