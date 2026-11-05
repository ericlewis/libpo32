// Package po32 provides libpo's PO-32 acoustic codec and drum synthesizer.
//
// The package wraps the C core to provide transfer frame building, packet
// encode/decode, DPSK audio rendering, audio decoding, and drum synthesis.
//
// Build with cgo and a C compiler; the C core is bundled within the module.
//
// Builder, Modulator, Demodulator and SynthVoice allocate C memory and implement Close
// for cleanup. A runtime finalizer is set as a safety net, but callers
// should use defer Close() for deterministic cleanup.
//
// Frame parsing uses a temporary C-allocated callback handle, and patch-text
// import uses a temporary C string. Both are freed before the call returns.
// Returned packet values and patch parameters use Go-managed memory.
//
// Raw packets are required for byte-exact backup preservation. StatePacket holds
// at most 16 chain entries, while a valid official backup contains 22. Typed
// pattern encoding also normalizes inactive trigger bytes to zero, although their
// selector/accent bits can be nonzero in backups. Parameter pairs, Reserved and
// the final accent mask are preserved. See core/docs/PO_FORMATS.md for details.
package po32
