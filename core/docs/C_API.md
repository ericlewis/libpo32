# C API guide

Include `po.h` for all families, or include only the headers your application
needs. Start with [installation](#install-and-link) and
[ownership and completion](#data-flow-and-ownership).

| API | Reference |
| --- | --- |
| PO-32 packets, mono modem and drum synthesis | [PO-32 C API](api/PO32.md) |
| PO-33/133 records, PCM sample conversion and pool edits | [PO-33 C API](api/PO33.md) |
| PO-35/137 records, speech analysis and rendering | [PO-35 C API](api/PO35.md) |
| PO-33/35 frame parsing, builders and stereo modem | [Shared stereo APIs](api/STEREO.md) |

Public headers define per-function contracts. Use the [format reference](PO_FORMATS.md)
for field meanings and the [command-line guides](../examples/README.md) for WAV workflows.

## Data flow and ownership

A backup passes through several representations. Keep them separate when
sizing buffers or editing data:

1. **Audio samples** are the recorded or generated modem signal. PO-32 uses
   mono audio; PO-33/133 and PO-35/137 use interleaved stereo audio.
2. **Frame bytes** contain the preamble, encoded packets and termination.
   A family parser checks the framing and delivers validated packets.
3. **Packet payloads** contain model-specific data. Some PO-33 logical records
   span multiple packets, so collect their payloads before decoding a record.
4. **Typed records** are convenient C views, not memory overlays for wire bytes.
   Use each record's codec to convert between the two representations.

The core does not open WAV files or audio devices. The application supplies
buffers and keeps them alive for as long as an operation borrows them. In
particular, a stereo modulator borrows its input frame across render calls and
resets. Parser callback pointers are borrowed only for the callback: copy a
packet struct if it must be retained.

For stereo receive, first recover wire bytes with the shared offline modem,
then pass them to the appropriate family parser. The modem can return bytes
beyond the transfer's end; recovery alone does not validate the complete
backup. For PO-33 and PO-35, check the parser status and `result.done`, and
check `result.stopped` when using a callback. Compare `result.frame_bytes`
with the input length only when trailing bytes must be rejected. A callback
can receive valid packets before a later error, so defer committing a complete
backup until parsing has finished successfully.

## Model-family APIs

Shared statuses and raw packets are in `po_common.h`. The `po_status_t`,
`po_packet_t` and `PO_*` names alias the existing PO-32 types and status values.

| Header | Role |
| --- | --- |
| [`po32.h`](../include/po32.h) | PO-32 packets, builders, mono modem and lossless control-byte views |
| [`po32_synth.h`](../include/po32_synth.h) | Drum synthesis |
| [`po33.h`](../include/po33.h) | PO-33/133 framing, samples, logical records, pool topology/edit transactions, control-nibble and per-step FX views |
| [`po35.h`](../include/po35.h) | PO-35/137 framing, speech/settings/pattern records and indexed-table views |
| [`po35_analysis.h`](../include/po35_analysis.h) | One-shot PCM-to-speech analysis with caller-owned workspace |
| [`po35_synth.h`](../include/po35_synth.h) | Borrowed-record speech voices and one-shot rendering |
| [`po_stereo.h`](../include/po_stereo.h) | Shared chunked stereo TX and caller-workspace offline RX |

All modules use caller-owned memory. Files, device access, normalization and
audio playback belong to the application. See [Architecture](ARCHITECTURE.md)
for build requirements and [Examples](../examples/README.md) for hosted use.

The C interface groups data and operations by family and domain: `*_t` types
hold records or working state, `*_decode`/`*_encode` convert serialized data,
and builders or `*_set_*` helpers edit supported musical structures. Existing
names remain compatible. Family-specific structs keep differing pattern and
speech formats explicit; they are not interchangeable wire overlays.

## Install and link

Install the built archive, public headers, CMake packages and pkg-config files:

```sh
cmake --install build --prefix "$PWD/install"
```

In a consuming CMake project:

```cmake
find_package(LibPO CONFIG REQUIRED)
target_link_libraries(app PRIVATE LibPO::po)
```

Pass the installation prefix in `CMAKE_PREFIX_PATH` when configuring that
project. For pkg-config consumers, put the installed pkg-config directory on
`PKG_CONFIG_PATH` if it is outside the system search path:

```sh
cc app.c $(pkg-config --cflags --libs po) -o app
```

Include `<po.h>` for all families or choose the headers listed above. The
archive retains the name `libpo32.a`; see [package compatibility](ARCHITECTURE.md#build-and-package-names)
for legacy CMake and pkg-config aliases.
