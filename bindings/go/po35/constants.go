package po35

const (
	PreambleByteCount           = 4096
	TagPatch             uint16 = 0x37b2
	TagTable7BE7         uint16 = 0x7be7
	TagTable1C7F         uint16 = 0x1c7f
	TagPatternEmpty      uint16 = 0x29ff
	TagPattern           uint16 = 0xccb2
	TagSpeechHeader      uint16 = 0xdc32
	TagSpeechData        uint16 = 0x73b0
	TagSpeechEnd         uint16 = 0x29a1
	TagSettings          uint16 = 0x84d8
	SpeechHeaderBytes           = 9
	SpeechRecordBytes           = 44
	SpeechSlotCount             = 15
	DrumCount                   = 16
	PatternStepCount            = 16
	PatternBytes                = 183
	SettingsBaseBytes           = 156
	ChainMaxCount               = 99
	AnalysisModelVersion        = 1
	AnalysisRecordBytes         = 44
	AnalysisMaxRecords          = 65535
	SynthModelVersion           = 1
	SynthRecordBytes            = 44
	SynthMaxOrder               = 30
	SynthDefaultSeed     uint32 = 350
)

// SynthBank chooses the native reconstruction rate and coefficient bank.
type SynthBank int

const (
	SynthBankLow  SynthBank = 0 // 10 coefficients at 8000 Hz.
	SynthBankHigh SynthBank = 1 // 30 coefficients at 24000 Hz.
)
