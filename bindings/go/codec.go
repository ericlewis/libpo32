package po32

/*
#include "po32.h"
*/
import "C"
import "unsafe"

// EncodePatchPacket encodes a patch packet to wire format.
func EncodePatchPacket(pkt *PatchPacket) (*Packet, error) {
	c := patchPacketToC(pkt)
	var out C.po32_packet_t
	s := C.po32_packet_encode(C.uint16_t(TagPatch), unsafe.Pointer(&c), &out)
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := packetFromC(&out)
	return &p, nil
}

// EncodeKnobPacket encodes a knob packet to wire format.
func EncodeKnobPacket(pkt *KnobPacket) (*Packet, error) {
	c := knobPacketToC(pkt)
	var out C.po32_packet_t
	s := C.po32_packet_encode(C.uint16_t(TagKnob), unsafe.Pointer(&c), &out)
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := packetFromC(&out)
	return &p, nil
}

// EncodeResetPacket encodes a reset packet to wire format.
func EncodeResetPacket(pkt *ResetPacket) (*Packet, error) {
	c := resetPacketToC(pkt)
	var out C.po32_packet_t
	s := C.po32_packet_encode(C.uint16_t(TagReset), unsafe.Pointer(&c), &out)
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := packetFromC(&out)
	return &p, nil
}

// EncodeStatePacket encodes a state packet to wire format.
func EncodeStatePacket(pkt *StatePacket) (*Packet, error) {
	c := statePacketToC(pkt)
	var out C.po32_packet_t
	s := C.po32_packet_encode(C.uint16_t(TagState), unsafe.Pointer(&c), &out)
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := packetFromC(&out)
	return &p, nil
}

// EncodePatternPacket encodes a pattern packet to wire format.
func EncodePatternPacket(pkt *PatternPacket) (*Packet, error) {
	c := patternPacketToC(pkt)
	var out C.po32_packet_t
	s := C.po32_packet_encode(C.uint16_t(TagPattern), unsafe.Pointer(&c), &out)
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := packetFromC(&out)
	return &p, nil
}

// DecodePatchPacket decodes a patch packet from payload bytes.
func DecodePatchPacket(data []byte) (*PatchPacket, error) {
	if len(data) == 0 {
		return nil, ErrInvalidArg
	}
	var c C.po32_patch_packet_t
	s := C.po32_packet_decode(C.uint16_t(TagPatch), (*C.uint8_t)(&data[0]), C.size_t(len(data)), unsafe.Pointer(&c))
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := patchPacketFromC(&c)
	return &p, nil
}

// DecodeKnobPacket decodes a knob packet from payload bytes.
func DecodeKnobPacket(data []byte) (*KnobPacket, error) {
	if len(data) == 0 {
		return nil, ErrInvalidArg
	}
	var c C.po32_knob_packet_t
	s := C.po32_packet_decode(C.uint16_t(TagKnob), (*C.uint8_t)(&data[0]), C.size_t(len(data)), unsafe.Pointer(&c))
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := knobPacketFromC(&c)
	return &p, nil
}

// DecodeResetPacket decodes a reset packet from payload bytes.
func DecodeResetPacket(data []byte) (*ResetPacket, error) {
	if len(data) == 0 {
		return nil, ErrInvalidArg
	}
	var c C.po32_reset_packet_t
	s := C.po32_packet_decode(C.uint16_t(TagReset), (*C.uint8_t)(&data[0]), C.size_t(len(data)), unsafe.Pointer(&c))
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := resetPacketFromC(&c)
	return &p, nil
}

// DecodeStatePacket decodes a state packet from payload bytes.
func DecodeStatePacket(data []byte) (*StatePacket, error) {
	if len(data) == 0 {
		return nil, ErrInvalidArg
	}
	var c C.po32_state_packet_t
	s := C.po32_packet_decode(C.uint16_t(TagState), (*C.uint8_t)(&data[0]), C.size_t(len(data)), unsafe.Pointer(&c))
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := statePacketFromC(&c)
	return &p, nil
}

// DecodePatternPacket decodes a pattern packet from payload bytes.
func DecodePatternPacket(data []byte) (*PatternPacket, error) {
	if len(data) == 0 {
		return nil, ErrInvalidArg
	}
	var c C.po32_pattern_packet_t
	s := C.po32_packet_decode(C.uint16_t(TagPattern), (*C.uint8_t)(&data[0]), C.size_t(len(data)), unsafe.Pointer(&c))
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	p := patternPacketFromC(&c)
	return &p, nil
}

// DecodePacketByTag decodes a Packet into the appropriate typed struct.
// Returns one of *PatchPacket, *KnobPacket, *ResetPacket, *StatePacket, or *PatternPacket.
func DecodePacketByTag(pkt *Packet) (any, error) {
	data := pkt.Payload[:pkt.PayloadLen]
	switch pkt.TagCode {
	case TagPatch:
		return DecodePatchPacket(data)
	case TagKnob:
		return DecodeKnobPacket(data)
	case TagReset:
		return DecodeResetPacket(data)
	case TagState:
		return DecodeStatePacket(data)
	case TagPattern:
		return DecodePatternPacket(data)
	default:
		return nil, ErrInvalidArg
	}
}

// EncodePatchRaw encodes 21 patch parameters to raw bytes.
func EncodePatchRaw(params *PatchParams) ([]byte, error) {
	c := patchParamsToC(params)
	out := make([]byte, ParamCount*2)
	var outLen C.size_t
	s := C.po32_encode_patch(&c, (*C.uint8_t)(&out[0]), C.size_t(len(out)), &outLen)
	if err := statusToError(int(s)); err != nil {
		return nil, err
	}
	return out[:int(outLen)], nil
}

// DecodePatchRaw decodes raw bytes into 21 patch parameters.
func DecodePatchRaw(data []byte) (PatchParams, error) {
	if len(data) == 0 {
		return PatchParams{}, ErrInvalidArg
	}
	var c C.po32_patch_params_t
	s := C.po32_decode_patch((*C.uint8_t)(&data[0]), C.size_t(len(data)), &c)
	if err := statusToError(int(s)); err != nil {
		return PatchParams{}, err
	}
	return patchParamsFromC(&c), nil
}
