// Package stereo transports raw backup wire bytes for the PO-33/133 and
// PO-35/137 families using the shared stereo DQPSK modem. A model-specific
// parser must validate recovered frames and establish completion. Sharing this
// transport does not imply shared record semantics. Existing hardware evidence
// covers specific PO-33 tests, not acceptance of every family or generated edit.
package stereo
