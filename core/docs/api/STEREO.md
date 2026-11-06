# Stereo framing and modem APIs

[API overview](../C_API.md) · [Documentation](../README.md)

## PO-33 and PO-35 Frames

`po33_frame_parse` and `po35_frame_parse` validate their model's counter
preamble, packet checksums and termination. Unknown packet tags remain raw.
Callbacks can receive a valid prefix before a later error. A successful status
alone does not establish completion: inspect `result.done` and `result.stopped`.
Trailing bytes are excluded from `result.frame_bytes`; compare that value with
the input length when exact consumption is required.

Both families provide `*_builder_init`, `*_builder_append_packet` and
`*_builder_finish`. Builders use caller-owned storage and reject capacity
errors before writing a partial packet. PO-33 emits its BEGIN/END packets
automatically; PO-35 emits its special final tail at finish. Do not append these
terminators as ordinary packets. Finish is idempotent.

## Shared Stereo Modem

`po_stereo_modulator_init` borrows a nonempty wire buffer for chunked rendering.
The buffer must remain alive and immutable whenever the state may render or
reset, including subsequent renders after reset. It accepts
44,100, 48,000 and 96,000 Hz and produces interleaved left/right floats.
`po_stereo_modulator_render_f32` capacities and output counts are **stereo
frames**, so a capacity of 256 requires storage for 512 floats. Chunk boundaries
do not change the waveform. The modem does not validate packet grammar.

The stereo receiver is offline and requires the complete capture. Call
`po_stereo_decode_requirements` for workspace and wire-buffer capacities, then
provide those buffers to `po_stereo_decode_f32`. Workspace sizes are element
counts: `double` elements for scalars, bytes for scratch and recovered wire.
Input samples remain unchanged, and all buffers must be nonoverlapping.

The receiver accepts finite interleaved float input at 32,000 through
192,000 Hz. Channel order is explicit. It finds the counter preamble and
recovers wire bytes, potentially including bytes beyond the final packet.
Always pass those bytes to the relevant model parser to establish completion.
