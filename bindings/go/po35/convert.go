package po35

/*
#include "po35.h"
*/
import "C"
import libpo "github.com/ericlewis/libpo32/bindings/go"

const maxInt = int(^uint(0) >> 1)

func sizeArg(n int) (C.size_t, error) {
	if n < 0 || uint64(C.size_t(n)) != uint64(n) {
		return 0, libpo.ErrRange
	}
	return C.size_t(n), nil
}
func sizeResult(n C.size_t) (int, error) {
	if uint64(n) > uint64(maxInt) {
		return 0, libpo.ErrRange
	}
	return int(n), nil
}
func bytePtr(b []byte) *C.uint8_t {
	if len(b) == 0 {
		return nil
	}
	return (*C.uint8_t)(&b[0])
}
func headerToC(p *SpeechHeader) C.po35_speech_header_t {
	c := C.po35_speech_header_t{slot: C.uint8_t(p.Slot), record_count: C.uint16_t(p.RecordCount), records_per_second: C.uint8_t(p.RecordsPerSecond)}
	for i := range p.Metadata {
		c.metadata[i] = C.uint8_t(p.Metadata[i])
	}
	return c
}
func headerFromC(c *C.po35_speech_header_t) *SpeechHeader {
	p := &SpeechHeader{Slot: uint8(c.slot), RecordCount: uint16(c.record_count), RecordsPerSecond: uint8(c.records_per_second)}
	for i := range p.Metadata {
		p.Metadata[i] = uint8(c.metadata[i])
	}
	return p
}
func recordToC(p *SpeechRecord) C.po35_speech_record_t {
	c := C.po35_speech_record_t{pitch: C.int16_t(p.Pitch), gain: C.uint8_t(p.Gain), unvoiced: C.uint8_t(p.Unvoiced)}
	for i := range p.LowLARRaw {
		c.low_lar_raw[i] = C.int8_t(p.LowLARRaw[i])
	}
	for i := range p.HighLARRaw {
		c.high_lar_raw[i] = C.int8_t(p.HighLARRaw[i])
	}
	return c
}
func recordFromC(c *C.po35_speech_record_t) *SpeechRecord {
	p := &SpeechRecord{Pitch: int16(c.pitch), Gain: uint8(c.gain), Unvoiced: uint8(c.unvoiced)}
	for i := range p.LowLARRaw {
		p.LowLARRaw[i] = int8(c.low_lar_raw[i])
	}
	for i := range p.HighLARRaw {
		p.HighLARRaw[i] = int8(c.high_lar_raw[i])
	}
	return p
}
func drumToC(p DrumControls) C.po35_drum_controls_t {
	return C.po35_drum_controls_t{pitch: C.uint8_t(p.Pitch), morph: C.uint8_t(p.Morph)}
}
func drumFromC(c C.po35_drum_controls_t) DrumControls {
	return DrumControls{Pitch: uint8(c.pitch), Morph: uint8(c.morph)}
}
func settingsToC(p *Settings) C.po35_settings_t {
	c := C.po35_settings_t{tempo: C.uint8_t(p.Tempo), chain_count: C.uint8_t(p.ChainCount)}
	for i := range p.Speech {
		for j := 0; j < 4; j++ {
			c.speech[i].parameters[j] = C.uint8_t(p.Speech[i].Parameters[j])
			c.speech[i].state_raw[j] = C.uint8_t(p.Speech[i].StateRaw[j])
		}
	}
	for i := range p.Drums {
		c.drums[i] = drumToC(p.Drums[i])
	}
	for i := range p.StateRaw {
		c.state_raw[i] = C.uint8_t(p.StateRaw[i])
	}
	for i := range p.Chain {
		c.chain[i] = C.uint8_t(p.Chain[i])
	}
	return c
}
func settingsFromC(c *C.po35_settings_t) *Settings {
	p := &Settings{Tempo: uint8(c.tempo), ChainCount: uint8(c.chain_count)}
	for i := range p.Speech {
		for j := 0; j < 4; j++ {
			p.Speech[i].Parameters[j] = uint8(c.speech[i].parameters[j])
			p.Speech[i].StateRaw[j] = uint8(c.speech[i].state_raw[j])
		}
	}
	for i := range p.Drums {
		p.Drums[i] = drumFromC(c.drums[i])
	}
	for i := range p.StateRaw {
		p.StateRaw[i] = uint8(c.state_raw[i])
	}
	for i := range p.Chain {
		p.Chain[i] = uint8(c.chain[i])
	}
	return p
}
func patternToC(p *Pattern) C.po35_pattern_t {
	c := C.po35_pattern_t{pattern_index: C.uint8_t(p.PatternIndex), derived_mask: C.uint16_t(p.DerivedMask)}
	for i := 0; i < PatternStepCount; i++ {
		c.speech_events[i] = C.uint8_t(p.SpeechEvents[i])
		c.speech_secondary_raw[i] = C.uint8_t(p.SpeechSecondaryRaw[i])
		c.drum_events[i] = C.uint8_t(p.DrumEvents[i])
		c.drums[i] = drumToC(p.Drums[i])
		c.index_pairs_raw[i] = C.uint8_t(p.IndexPairsRaw[i])
		c.step_state_raw[i] = C.uint8_t(p.StepStateRaw[i])
		for j := 0; j < 4; j++ {
			c.speech_parameters[i][j] = C.uint8_t(p.SpeechParameters[i][j])
		}
	}
	for i := range p.TailRaw {
		c.tail_raw[i] = C.uint8_t(p.TailRaw[i])
	}
	return c
}
func patternFromC(c *C.po35_pattern_t) *Pattern {
	p := &Pattern{PatternIndex: uint8(c.pattern_index), DerivedMask: uint16(c.derived_mask)}
	for i := 0; i < PatternStepCount; i++ {
		p.SpeechEvents[i] = uint8(c.speech_events[i])
		p.SpeechSecondaryRaw[i] = uint8(c.speech_secondary_raw[i])
		p.DrumEvents[i] = uint8(c.drum_events[i])
		p.Drums[i] = drumFromC(c.drums[i])
		p.IndexPairsRaw[i] = uint8(c.index_pairs_raw[i])
		p.StepStateRaw[i] = uint8(c.step_state_raw[i])
		for j := 0; j < 4; j++ {
			p.SpeechParameters[i][j] = uint8(c.speech_parameters[i][j])
		}
	}
	for i := range p.TailRaw {
		p.TailRaw[i] = uint8(c.tail_raw[i])
	}
	return p
}
