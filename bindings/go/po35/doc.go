// Package po35 provides C-backed PO-35/PO-137 structural codecs, backup framing,
// and experimental model-1 speech analysis and reconstruction. Structural
// codecs preserve unknown bytes. Speech synthesis is an approximation, not the
// original device decoder; output is finite but is not clipped or normalized.
// Shared transport and compatible corpus records do not establish hardware
// identity, exact voice/effect behavior or physical cross-loading compatibility.
// See core/docs/PO_FORMATS.md for field meanings and evidence limits.
package po35
