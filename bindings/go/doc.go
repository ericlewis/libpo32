// Package libpo provides shared packet types and status errors for libpo's
// model-specific Go packages: po32, po33, po35, and stereo.
//
// The module bundles the C core and requires cgo and a C99 compiler. Each model
// package keeps its own data format and synthesis APIs. None controls audio
// devices or reads WAV files. Successful software decoding does not establish
// physical-device acceptance of a generated transfer. Existing hardware evidence
// covers specific PO-33 tests, not every model or generated edit.
//
// Raw Packet values preserve payloads that typed model records may not represent;
// see core/docs/PO_FORMATS.md for field meanings and preservation limits.
package libpo
