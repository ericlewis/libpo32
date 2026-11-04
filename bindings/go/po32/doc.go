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
package po32
