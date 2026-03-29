package po32

// MorphPair holds a morph flag and value.
type MorphPair struct {
	Flag  uint8
	Morph uint8
}

// PatchParams holds the 21 normalized drum voice parameters (all 0.0–1.0).
type PatchParams struct {
	OscWave, OscFreq, OscAtk, OscDcy float32
	ModMode, ModRate, ModAmt          float32
	NFilMod, NFilFrq, NFilQ           float32
	NEnvMod, NEnvAtk, NEnvDcy         float32
	Mix, DistAmt, EQFreq, EQGain      float32
	Level, OscVel, NVel, ModVel       float32
}

// PacketTrailer holds CRC trailer state from a decoded packet.
type PacketTrailer struct {
	Lo, Hi       uint8
	State        uint16
	MatchesState bool
}

// Packet is a decoded wire packet with an owned payload buffer.
type Packet struct {
	TagCode    uint16
	Offset     int
	PayloadLen int
	Payload    [MaxPayload]byte
	Trailer    PacketTrailer
}

// FinalTail is the decoded frame terminator.
type FinalTail struct {
	MarkerC3, Marker71 uint8
	SpecialByte        uint8
	FinalLo, FinalHi   uint8
	StateBeforeSpecial uint16
	StateAfterSpecial  uint16
	FinalState         uint16
}

// PatchPacket is a typed patch transfer packet.
type PatchPacket struct {
	Instrument uint8
	Side       PatchSide
	Params     PatchParams
}

// KnobPacket is a typed knob control packet.
type KnobPacket struct {
	Instrument uint8
	Kind       KnobKind
	Value      uint8
}

// ResetPacket is a typed instrument reset packet.
type ResetPacket struct {
	Instrument uint8
}

// StatePacket holds synth state: morph defaults, tempo, swing, and pattern list.
type StatePacket struct {
	MorphPairs     [StateMorphPairCount]MorphPair
	Tempo          uint8
	SwingTimes12   uint8
	PatternNumbers [PatternStepCount]uint8
	PatternCount   int
}

// PatternStep describes one trigger slot in a pattern lane.
type PatternStep struct {
	Instrument uint8
	FillRate   uint8
	Accent     bool
}

// PatternPacket holds a full 16-step, 4-lane pattern.
type PatternPacket struct {
	PatternNumber uint8
	Steps         [PatternLaneCount * PatternStepCount]PatternStep
	MorphLanes    [PatternLaneCount * PatternStepCount]MorphPair
	Reserved      [PatternReservedCount]uint8
	AccentBits    uint16
}

// DecodeResult holds the outcome of an audio-to-frame decode.
type DecodeResult struct {
	PacketCount int
	Done        bool
	Tail        FinalTail
}

// Synth is a drum voice synthesizer instance.
type Synth struct {
	sampleRate uint32
}
