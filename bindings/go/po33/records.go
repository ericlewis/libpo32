package po33

/*
#include "po33.h"
*/
import "C"

// DecodeSettings decodes exactly SettingsBytes bytes, preserving every raw byte.
func DecodeSettings(data []byte) (Settings, error) {
	if len(data) != SettingsBytes {
		return Settings{}, ErrFrame
	}
	var native C.po33_settings_t
	if err := Status(C.po33_settings_decode((*C.uint8_t)(&data[0]), C.size_t(len(data)), &native)).Err(); err != nil {
		return Settings{}, err
	}
	var value Settings
	for i := range value.PrefixRaw {
		value.PrefixRaw[i] = byte(native.prefix_raw[i])
	}
	for i := range value.Chain {
		value.Chain[i] = byte(native.chain[i])
	}
	value.ChainLength, value.Tempo, value.SwingRaw = uint8(native.chain_length), uint8(native.tempo), uint8(native.swing_raw)
	for i := range value.StateRaw {
		value.StateRaw[i] = byte(native.state_raw[i])
	}
	for i := range value.SuffixRaw {
		value.SuffixRaw[i] = byte(native.suffix_raw[i])
	}
	return value, nil
}

// EncodeSettings encodes all settings bytes. ChainLength must not exceed 128.
func EncodeSettings(value *Settings) ([]byte, error) {
	if value == nil {
		return nil, ErrInvalidArg
	}
	var native C.po33_settings_t
	for i, v := range value.PrefixRaw {
		native.prefix_raw[i] = C.uint8_t(v)
	}
	for i, v := range value.Chain {
		native.chain[i] = C.uint8_t(v)
	}
	native.chain_length, native.tempo, native.swing_raw = C.uint8_t(value.ChainLength), C.uint8_t(value.Tempo), C.uint8_t(value.SwingRaw)
	for i, v := range value.StateRaw {
		native.state_raw[i] = C.uint8_t(v)
	}
	for i, v := range value.SuffixRaw {
		native.suffix_raw[i] = C.uint8_t(v)
	}
	out := make([]byte, SettingsBytes)
	if err := Status(C.po33_settings_encode(&native, (*C.uint8_t)(&out[0]), C.size_t(len(out)))).Err(); err != nil {
		return nil, err
	}
	return out, nil
}

// DecodePattern decodes exactly PatternBytes bytes into step/sound events.
func DecodePattern(data []byte) (Pattern, error) {
	if len(data) != PatternBytes {
		return Pattern{}, ErrFrame
	}
	var native C.po33_pattern_t
	if err := Status(C.po33_pattern_decode((*C.uint8_t)(&data[0]), C.size_t(len(data)), &native)).Err(); err != nil {
		return Pattern{}, err
	}
	var value Pattern
	for step := range value.Events {
		for sound := range value.Events[step] {
			for i := range value.Events[step][sound].EventRaw {
				value.Events[step][sound].EventRaw[i] = byte(native.events[step][sound].event_raw[i])
			}
			value.Events[step][sound].ControlRaw = uint8(native.events[step][sound].control_raw)
		}
	}
	for i := range value.FooterRaw {
		value.FooterRaw[i] = byte(native.footer_raw[i])
	}
	return value, nil
}

// EncodePattern encodes all event bytes, controls, and the raw footer.
func EncodePattern(value *Pattern) ([]byte, error) {
	if value == nil {
		return nil, ErrInvalidArg
	}
	var native C.po33_pattern_t
	for step := range value.Events {
		for sound := range value.Events[step] {
			for i, v := range value.Events[step][sound].EventRaw {
				native.events[step][sound].event_raw[i] = C.uint8_t(v)
			}
			native.events[step][sound].control_raw = C.uint8_t(value.Events[step][sound].ControlRaw)
		}
	}
	for i, v := range value.FooterRaw {
		native.footer_raw[i] = C.uint8_t(v)
	}
	out := make([]byte, PatternBytes)
	if err := Status(C.po33_pattern_encode(&native, (*C.uint8_t)(&out[0]), C.size_t(len(out)))).Err(); err != nil {
		return nil, err
	}
	return out, nil
}

// DecodeSoundHeader decodes exactly SoundHeaderBytes bytes of raw metadata.
func DecodeSoundHeader(data []byte) (SoundHeader, error) {
	if len(data) != SoundHeaderBytes {
		return SoundHeader{}, ErrFrame
	}
	var native C.po33_sound_header_t
	if err := Status(C.po33_sound_header_decode((*C.uint8_t)(&data[0]), C.size_t(len(data)), &native)).Err(); err != nil {
		return SoundHeader{}, err
	}
	return SoundHeader{RangeStartRaw: uint32(native.range_start_raw), RangeEndRaw: uint32(native.range_end_raw), SourceSelector: uint8(native.source_selector)}, nil
}

// EncodeSoundHeader encodes raw range metadata and source selection.
func EncodeSoundHeader(value *SoundHeader) ([]byte, error) {
	if value == nil {
		return nil, ErrInvalidArg
	}
	native := C.po33_sound_header_t{range_start_raw: C.uint32_t(value.RangeStartRaw), range_end_raw: C.uint32_t(value.RangeEndRaw), source_selector: C.uint8_t(value.SourceSelector)}
	out := make([]byte, SoundHeaderBytes)
	if err := Status(C.po33_sound_header_encode(&native, (*C.uint8_t)(&out[0]), C.size_t(len(out)))).Err(); err != nil {
		return nil, err
	}
	return out, nil
}
