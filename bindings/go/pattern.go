package po32

/*
#include "po32.h"
*/
import "C"

// NewPattern returns a zero-initialized pattern with the given number.
func NewPattern(patternNumber uint8) *PatternPacket {
	p := &PatternPacket{PatternNumber: patternNumber}
	return p
}

// Init reinitializes the pattern with the given number, clearing all data.
func (p *PatternPacket) Init(patternNumber uint8) {
	*p = PatternPacket{PatternNumber: patternNumber}
}

// Clear removes all triggers and accents, preserving the pattern number.
func (p *PatternPacket) Clear() {
	pn := p.PatternNumber
	*p = PatternPacket{PatternNumber: pn}
}

// SetTrigger places an instrument on its fixed lane at the given step.
// Instrument must be 1–16, step 0–15, fillRate 1–15.
func (p *PatternPacket) SetTrigger(step, instrument, fillRate uint8) error {
	c := patternPacketToC(p)
	s := C.po32_pattern_set_trigger(&c, C.uint8_t(step), C.uint8_t(instrument), C.uint8_t(fillRate))
	if err := statusToError(int(s)); err != nil {
		return err
	}
	*p = patternPacketFromC(&c)
	return nil
}

// ClearTrigger removes the trigger at the given step and lane (0–3).
func (p *PatternPacket) ClearTrigger(step, lane uint8) error {
	c := patternPacketToC(p)
	s := C.po32_pattern_clear_trigger(&c, C.uint8_t(step), C.uint8_t(lane))
	if err := statusToError(int(s)); err != nil {
		return err
	}
	*p = patternPacketFromC(&c)
	return nil
}

// ClearStep removes all triggers from the given step (0–15).
func (p *PatternPacket) ClearStep(step uint8) error {
	c := patternPacketToC(p)
	s := C.po32_pattern_clear_step(&c, C.uint8_t(step))
	if err := statusToError(int(s)); err != nil {
		return err
	}
	*p = patternPacketFromC(&c)
	return nil
}

// SetAccent enables or disables the accent on a step (0–15).
func (p *PatternPacket) SetAccent(step uint8, enabled bool) error {
	e := C.int(0)
	if enabled {
		e = 1
	}
	c := patternPacketToC(p)
	s := C.po32_pattern_set_accent(&c, C.uint8_t(step), e)
	if err := statusToError(int(s)); err != nil {
		return err
	}
	*p = patternPacketFromC(&c)
	return nil
}

// PatternTriggerLane returns the fixed lane index (0–3) for an instrument (1–16).
func PatternTriggerLane(instrument uint8) (uint8, error) {
	var lane C.uint8_t
	s := C.po32_pattern_trigger_lane(C.uint8_t(instrument), &lane)
	if err := statusToError(int(s)); err != nil {
		return 0, err
	}
	return uint8(lane), nil
}

// PatternTriggerEncode encodes a trigger byte from its components.
func PatternTriggerEncode(instrument, fillRate uint8, accent bool) (uint8, error) {
	a := C.int(0)
	if accent {
		a = 1
	}
	var trigger C.uint8_t
	s := C.po32_pattern_trigger_encode(C.uint8_t(instrument), C.uint8_t(fillRate), a, &trigger)
	if err := statusToError(int(s)); err != nil {
		return 0, err
	}
	return uint8(trigger), nil
}

// PatternTriggerDecode decodes a trigger byte into its components.
func PatternTriggerDecode(lane, trigger uint8) (instrument, fillRate uint8, accent bool, err error) {
	var ci, cf C.uint8_t
	var ca C.int
	s := C.po32_pattern_trigger_decode(C.uint8_t(lane), C.uint8_t(trigger), &ci, &cf, &ca)
	if e := statusToError(int(s)); e != nil {
		return 0, 0, false, e
	}
	return uint8(ci), uint8(cf), ca != 0, nil
}
