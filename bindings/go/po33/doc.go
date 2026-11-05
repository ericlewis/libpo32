// Package po33 provides the PO-33/PO-133 backup codec, lossless
// logical records, and sample conversion. It does not perform device I/O.
// Existing physical evidence covers specific PO-33 tests, not every generated
// edit or separate PO-133 hardware acceptance.
//
// Pattern records retain parameter-only rows with zero control bytes. Sample
// aliases reference whole stored pools, not their owners' trimmed intervals.
// Musical scales, several field meanings and trim endpoint inclusion remain
// unresolved; see core/docs/PO_FORMATS.md for the canonical field map.
package po33
