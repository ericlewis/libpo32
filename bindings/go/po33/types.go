package po33

import libpo "github.com/ericlewis/libpo32/bindings/go"

const (
	PreambleBytes            = 4096
	SettingsBytes            = 510
	PatternBytes             = 1032
	SoundHeaderBytes         = 9
	StepCount                = 16
	SoundCount               = 16
	ChainCapacity            = 128
	MaxPayload               = libpo.MaxPayload
	InitialState     uint16  = 0x1D0F
	SampleRateHz     float32 = 23437.5
	TagBegin         uint16  = 0x985B
	TagSettings      uint16  = 0x7D5C
	TagPatternSelect uint16  = 0xA041
	TagPatternData   uint16  = 0xC498
	TagSoundSelect   uint16  = 0x68B5
	TagSoundData     uint16  = 0xEF78
	TagEnd           uint16  = 0x826F
)

// Status and Packet are shared across model packages.
type Status = libpo.Status
type Packet = libpo.Packet
type PacketTrailer = libpo.PacketTrailer

const (
	OK                = libpo.OK
	ErrInvalidArg     = libpo.ErrInvalidArg
	ErrRange          = libpo.ErrRange
	ErrBufferTooSmall = libpo.ErrBufferTooSmall
	ErrFrame          = libpo.ErrFrame
	ErrParse          = libpo.ErrParse
)

// Settings preserves all bytes, including unused chain entries and raw state.
type Settings struct {
	PrefixRaw                    [255]byte
	Chain                        [ChainCapacity]byte
	ChainLength, Tempo, SwingRaw uint8
	StateRaw                     [16]byte
	SuffixRaw                    [108]byte
}

// PatternEvent preserves event bytes and the context-dependent control byte.
type PatternEvent struct {
	EventRaw   [3]byte
	ControlRaw uint8
}

// Pattern contains events indexed by step, then sound, and the raw footer.
type Pattern struct {
	Events    [StepCount][SoundCount]PatternEvent
	FooterRaw [8]byte
}

// SoundHeader preserves raw range metadata without inferring a trim convention.
type SoundHeader struct {
	RangeStartRaw, RangeEndRaw uint32
	SourceSelector             uint8
}

// DecodeResult describes validated progress. Only Done establishes a complete
// frame. Stopped means the callback returned false, even if it stopped on END.
type DecodeResult struct {
	PacketCount int
	FrameBytes  int
	Done        bool
	FinalState  uint16
	Stopped     bool
}
