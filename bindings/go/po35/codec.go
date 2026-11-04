package po35

/*
#include "po35.h"
*/
import "C"
import libpo "github.com/ericlewis/libpo32/bindings/go"

// DecodeSpeechHeader decodes exactly nine bytes, preserving metadata.
func DecodeSpeechHeader(data []byte) (*SpeechHeader, error) {
	var c C.po35_speech_header_t
	if err := libpo.Status(C.po35_speech_header_decode(bytePtr(data), C.size_t(len(data)), &c)).Err(); err != nil {
		return nil, err
	}
	return headerFromC(&c), nil
}

// EncodeSpeechHeader encodes a header without interpreting its metadata.
func EncodeSpeechHeader(p *SpeechHeader) ([]byte, error) {
	if p == nil {
		return nil, libpo.ErrInvalidArg
	}
	c := headerToC(p)
	out := make([]byte, SpeechHeaderBytes)
	if err := libpo.Status(C.po35_speech_header_encode(&c, bytePtr(out), C.size_t(len(out)))).Err(); err != nil {
		return nil, err
	}
	return out, nil
}

// DecodeSpeechRecord decodes exactly 44 bytes without altering coefficient values.
func DecodeSpeechRecord(data []byte) (*SpeechRecord, error) {
	var c C.po35_speech_record_t
	if err := libpo.Status(C.po35_speech_record_decode(bytePtr(data), C.size_t(len(data)), &c)).Err(); err != nil {
		return nil, err
	}
	return recordFromC(&c), nil
}

// EncodeSpeechRecord encodes the stored pitch, gain, and both coefficient banks.
func EncodeSpeechRecord(p *SpeechRecord) ([]byte, error) {
	if p == nil {
		return nil, libpo.ErrInvalidArg
	}
	c := recordToC(p)
	out := make([]byte, SpeechRecordBytes)
	if err := libpo.Status(C.po35_speech_record_encode(&c, bytePtr(out), C.size_t(len(out)))).Err(); err != nil {
		return nil, err
	}
	return out, nil
}

// DecodeSettings validates the stored chain count against the payload length.
func DecodeSettings(data []byte) (*Settings, error) {
	var c C.po35_settings_t
	if err := libpo.Status(C.po35_settings_decode(bytePtr(data), C.size_t(len(data)), &c)).Err(); err != nil {
		return nil, err
	}
	return settingsFromC(&c), nil
}

// EncodeSettings encodes all controls and ChainCount chain entries verbatim.
func EncodeSettings(p *Settings) ([]byte, error) {
	if p == nil {
		return nil, libpo.ErrInvalidArg
	}
	c := settingsToC(p)
	out := make([]byte, libpo.MaxPayload)
	var n C.size_t
	if err := libpo.Status(C.po35_settings_encode(&c, bytePtr(out), C.size_t(len(out)), &n)).Err(); err != nil {
		return nil, err
	}
	return out[:int(n)], nil
}

// DecodePattern decodes exactly 183 bytes, including its stored derived mask.
func DecodePattern(data []byte) (*Pattern, error) {
	var c C.po35_pattern_t
	if err := libpo.Status(C.po35_pattern_decode(bytePtr(data), C.size_t(len(data)), &c)).Err(); err != nil {
		return nil, err
	}
	return patternFromC(&c), nil
}

// EncodePattern preserves unknown bytes and does not recompute DerivedMask.
func EncodePattern(p *Pattern) ([]byte, error) {
	if p == nil {
		return nil, libpo.ErrInvalidArg
	}
	c := patternToC(p)
	out := make([]byte, PatternBytes)
	if err := libpo.Status(C.po35_pattern_encode(&c, bytePtr(out), C.size_t(len(out)))).Err(); err != nil {
		return nil, err
	}
	return out, nil
}
