package po32

/*
#include "po32.h"
#include "po32_synth.h"
*/
import "C"

func patchParamsToC(p *PatchParams) C.po32_patch_params_t {
	var c C.po32_patch_params_t
	c.OscWave = C.float(p.OscWave)
	c.OscFreq = C.float(p.OscFreq)
	c.OscAtk = C.float(p.OscAtk)
	c.OscDcy = C.float(p.OscDcy)
	c.ModMode = C.float(p.ModMode)
	c.ModRate = C.float(p.ModRate)
	c.ModAmt = C.float(p.ModAmt)
	c.NFilMod = C.float(p.NFilMod)
	c.NFilFrq = C.float(p.NFilFrq)
	c.NFilQ = C.float(p.NFilQ)
	c.NEnvMod = C.float(p.NEnvMod)
	c.NEnvAtk = C.float(p.NEnvAtk)
	c.NEnvDcy = C.float(p.NEnvDcy)
	c.Mix = C.float(p.Mix)
	c.DistAmt = C.float(p.DistAmt)
	c.EQFreq = C.float(p.EQFreq)
	c.EQGain = C.float(p.EQGain)
	c.Level = C.float(p.Level)
	c.OscVel = C.float(p.OscVel)
	c.NVel = C.float(p.NVel)
	c.ModVel = C.float(p.ModVel)
	return c
}

func patchParamsFromC(c *C.po32_patch_params_t) PatchParams {
	return PatchParams{
		OscWave: float32(c.OscWave), OscFreq: float32(c.OscFreq),
		OscAtk: float32(c.OscAtk), OscDcy: float32(c.OscDcy),
		ModMode: float32(c.ModMode), ModRate: float32(c.ModRate), ModAmt: float32(c.ModAmt),
		NFilMod: float32(c.NFilMod), NFilFrq: float32(c.NFilFrq), NFilQ: float32(c.NFilQ),
		NEnvMod: float32(c.NEnvMod), NEnvAtk: float32(c.NEnvAtk), NEnvDcy: float32(c.NEnvDcy),
		Mix: float32(c.Mix), DistAmt: float32(c.DistAmt),
		EQFreq: float32(c.EQFreq), EQGain: float32(c.EQGain),
		Level: float32(c.Level), OscVel: float32(c.OscVel),
		NVel: float32(c.NVel), ModVel: float32(c.ModVel),
	}
}

func morphPairToC(m *MorphPair) C.po32_morph_pair_t {
	return C.po32_morph_pair_t{flag: C.uint8_t(m.Flag), morph: C.uint8_t(m.Morph)}
}

func morphPairFromC(c *C.po32_morph_pair_t) MorphPair {
	return MorphPair{Flag: uint8(c.flag), Morph: uint8(c.morph)}
}

func packetFromC(c *C.po32_packet_t) Packet {
	var p Packet
	p.TagCode = uint16(c.tag_code)
	p.Offset = int(c.offset)
	p.PayloadLen = int(c.payload_len)
	for i := 0; i < int(c.payload_len) && i < MaxPayload; i++ {
		p.Payload[i] = uint8(c.payload[i])
	}
	p.Trailer.Lo = uint8(c.trailer.lo)
	p.Trailer.Hi = uint8(c.trailer.hi)
	p.Trailer.State = uint16(c.trailer.state)
	p.Trailer.MatchesState = c.trailer.matches_state != 0
	return p
}

func packetToC(p *Packet) C.po32_packet_t {
	var c C.po32_packet_t
	c.tag_code = C.uint16_t(p.TagCode)
	c.offset = C.size_t(p.Offset)
	c.payload_len = C.size_t(p.PayloadLen)
	for i := 0; i < p.PayloadLen && i < MaxPayload; i++ {
		c.payload[i] = C.uint8_t(p.Payload[i])
	}
	ms := 0
	if p.Trailer.MatchesState {
		ms = 1
	}
	c.trailer.lo = C.uint8_t(p.Trailer.Lo)
	c.trailer.hi = C.uint8_t(p.Trailer.Hi)
	c.trailer.state = C.uint16_t(p.Trailer.State)
	c.trailer.matches_state = C.int(ms)
	return c
}

func finalTailFromC(c *C.po32_final_tail_t) FinalTail {
	return FinalTail{
		MarkerC3: uint8(c.marker_c3), Marker71: uint8(c.marker_71),
		SpecialByte: uint8(c.special_byte),
		FinalLo: uint8(c.final_lo), FinalHi: uint8(c.final_hi),
		StateBeforeSpecial: uint16(c.state_before_special),
		StateAfterSpecial:  uint16(c.state_after_special),
		FinalState:         uint16(c.final_state),
	}
}

func patchPacketToC(p *PatchPacket) C.po32_patch_packet_t {
	var c C.po32_patch_packet_t
	c.instrument = C.uint8_t(p.Instrument)
	c.side = C.po32_patch_side_t(p.Side)
	c.params = patchParamsToC(&p.Params)
	return c
}

func patchPacketFromC(c *C.po32_patch_packet_t) PatchPacket {
	return PatchPacket{
		Instrument: uint8(c.instrument),
		Side:       PatchSide(c.side),
		Params:     patchParamsFromC(&c.params),
	}
}

func knobPacketToC(p *KnobPacket) C.po32_knob_packet_t {
	return C.po32_knob_packet_t{
		instrument: C.uint8_t(p.Instrument),
		kind:       C.po32_knob_kind_t(p.Kind),
		value:      C.uint8_t(p.Value),
	}
}

func knobPacketFromC(c *C.po32_knob_packet_t) KnobPacket {
	return KnobPacket{
		Instrument: uint8(c.instrument),
		Kind:       KnobKind(c.kind),
		Value:      uint8(c.value),
	}
}

func resetPacketToC(p *ResetPacket) C.po32_reset_packet_t {
	return C.po32_reset_packet_t{instrument: C.uint8_t(p.Instrument)}
}

func resetPacketFromC(c *C.po32_reset_packet_t) ResetPacket {
	return ResetPacket{Instrument: uint8(c.instrument)}
}

func statePacketToC(p *StatePacket) C.po32_state_packet_t {
	var c C.po32_state_packet_t
	for i := 0; i < StateMorphPairCount; i++ {
		c.morph_pairs[i] = morphPairToC(&p.MorphPairs[i])
	}
	c.tempo = C.uint8_t(p.Tempo)
	c.swing_times_12 = C.uint8_t(p.SwingTimes12)
	c.pattern_count = C.size_t(p.PatternCount)
	for i := 0; i < PatternStepCount; i++ {
		c.pattern_numbers[i] = C.uint8_t(p.PatternNumbers[i])
	}
	return c
}

func statePacketFromC(c *C.po32_state_packet_t) StatePacket {
	var p StatePacket
	for i := 0; i < StateMorphPairCount; i++ {
		p.MorphPairs[i] = morphPairFromC(&c.morph_pairs[i])
	}
	p.Tempo = uint8(c.tempo)
	p.SwingTimes12 = uint8(c.swing_times_12)
	p.PatternCount = int(c.pattern_count)
	for i := 0; i < PatternStepCount; i++ {
		p.PatternNumbers[i] = uint8(c.pattern_numbers[i])
	}
	return p
}

func patternStepToC(s *PatternStep) C.po32_pattern_step_t {
	a := C.int(0)
	if s.Accent {
		a = 1
	}
	return C.po32_pattern_step_t{
		instrument: C.uint8_t(s.Instrument),
		fill_rate:  C.uint8_t(s.FillRate),
		accent:     a,
	}
}

func patternStepFromC(c *C.po32_pattern_step_t) PatternStep {
	return PatternStep{
		Instrument: uint8(c.instrument),
		FillRate:   uint8(c.fill_rate),
		Accent:     c.accent != 0,
	}
}

func patternPacketToC(p *PatternPacket) C.po32_pattern_packet_t {
	var c C.po32_pattern_packet_t
	c.pattern_number = C.uint8_t(p.PatternNumber)
	for i := range p.Steps {
		c.steps[i] = patternStepToC(&p.Steps[i])
	}
	for i := range p.MorphLanes {
		c.morph_lanes[i] = morphPairToC(&p.MorphLanes[i])
	}
	for i := range p.Reserved {
		c.reserved[i] = C.uint8_t(p.Reserved[i])
	}
	c.accent_bits = C.uint16_t(p.AccentBits)
	return c
}

func patternPacketFromC(c *C.po32_pattern_packet_t) PatternPacket {
	var p PatternPacket
	p.PatternNumber = uint8(c.pattern_number)
	for i := range p.Steps {
		p.Steps[i] = patternStepFromC(&c.steps[i])
	}
	for i := range p.MorphLanes {
		p.MorphLanes[i] = morphPairFromC(&c.morph_lanes[i])
	}
	for i := range p.Reserved {
		p.Reserved[i] = uint8(c.reserved[i])
	}
	p.AccentBits = uint16(c.accent_bits)
	return p
}

func decodeResultFromC(c *C.po32_decode_result_t) DecodeResult {
	return DecodeResult{
		PacketCount: int(c.packet_count),
		Done:        c.done != 0,
		Tail:        finalTailFromC(&c.tail),
	}
}
