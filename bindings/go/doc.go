// Package po32 provides Go bindings for the libpo32 PO-32 acoustic codec.
//
// The package wraps the C core to provide transfer frame building, packet
// encode/decode, DPSK audio rendering, audio decoding, and drum synthesis.
//
// Builder and Modulator allocate C memory internally and implement Close
// for cleanup. A runtime finalizer is set as a safety net, but callers
// should use defer Close() for deterministic cleanup.
//
// All other functions and types use Go-managed memory exclusively.
package po32
