package po32

const (
	PreambleByteCount          = 128
	InitialState        uint16 = 0x1D0F
	NativeBaud          float32 = 1201.923076923077
	CarrierCyclesPerSym        = 3
	NativeBlockSize            = 64
	ParamCount                 = 21
	StateMorphPairCount        = 16
	PatternLaneCount           = 4
	PatternStepCount           = 16
	PatternReservedCount       = 16
	PatternPayloadBytes        = 211
	PatchPayloadBytes          = 43
	MaxPayload                 = 255
	FinalTailBytes             = 5

	TagPatch   uint16 = 0x37B2
	TagReset   uint16 = 0x4AB4
	TagPattern uint16 = 0xD022
	TagErase   uint16 = 0xB892
	TagState   uint16 = 0x505A
	TagKnob    uint16 = 0xA354
)

// PatchSide selects the left or right morph endpoint.
type PatchSide int

const (
	PatchLeft  PatchSide = 0
	PatchRight PatchSide = 1
)

// KnobKind selects pitch or morph knob control.
type KnobKind int

const (
	KnobPitch KnobKind = 0
	KnobMorph KnobKind = 1
)
