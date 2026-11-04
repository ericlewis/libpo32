package po35

// SpeechHeader preserves the nine-byte speech group header.
type SpeechHeader struct {
	Slot             uint8
	RecordCount      uint16
	RecordsPerSecond uint8
	Metadata         [5]uint8
}

// SpeechRecord stores both LPC coefficient banks without normalization.
type SpeechRecord struct {
	Pitch      int16 // Signed Q11 octaves relative to C4 in reconstruction model 1.
	Gain       uint8
	Unvoiced   uint8
	LowLARRaw  [10]int8
	HighLARRaw [30]int8
}

// SpeechControls preserves four unnamed parameters and four state bytes.
type SpeechControls struct {
	Parameters [4]uint8
	StateRaw   [4]uint8
}

// DrumControls holds the stored pitch and morph bytes.
type DrumControls struct{ Pitch, Morph uint8 }

// Settings preserves the settings payload and zero-based, repeated chain entries.
// Only the first ChainCount entries are encoded; ChainCount must be at most 99.
type Settings struct {
	Speech     [SpeechSlotCount]SpeechControls
	Drums      [DrumCount]DrumControls
	Tempo      uint8
	StateRaw   [2]uint8
	ChainCount uint8
	Chain      [ChainMaxCount]uint8
}

// Pattern preserves all 183 payload bytes; DerivedMask is never recomputed.
type Pattern struct {
	PatternIndex       uint8
	SpeechEvents       [PatternStepCount]uint8
	SpeechSecondaryRaw [PatternStepCount]uint8
	SpeechParameters   [PatternStepCount][4]uint8
	DrumEvents         [PatternStepCount]uint8
	Drums              [PatternStepCount]DrumControls
	IndexPairsRaw      [PatternStepCount]uint8
	StepStateRaw       [PatternStepCount]uint8
	TailRaw            [4]uint8
	DerivedMask        uint16
}

// DecodeResult describes the validated prefix, including on parse failure.
type DecodeResult struct {
	PacketCount int
	FrameBytes  int
	Done        bool
	FinalState  uint16
	Stopped     bool
}
