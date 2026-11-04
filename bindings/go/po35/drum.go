package po35

import (
	libpo "github.com/ericlewis/libpo32/bindings/go"
	"github.com/ericlewis/libpo32/bindings/go/po32"
)

// DrumPatch is the shared PO-32-style patch carried by PO-35 tag 0x37b2.
type DrumPatch = po32.PatchPacket

// EncodeDrumPatch uses the shared PO-32 patch codec. No speech conversion or
// model-specific reinterpretation is performed.
func EncodeDrumPatch(p *DrumPatch) (*libpo.Packet, error) {
	if p == nil {
		return nil, libpo.ErrInvalidArg
	}
	return po32.EncodePatchPacket(p)
}

// DecodeDrumPatch uses the shared PO-32 codec on the patch payload.
func DecodeDrumPatch(data []byte) (*DrumPatch, error) { return po32.DecodePatchPacket(data) }
