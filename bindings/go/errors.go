package po32

// Status represents a libpo32 error code and implements the error interface.
type Status int

const (
	ErrInvalidArg    Status = -1
	ErrRange         Status = -2
	ErrBufferTooSmall Status = -3
	ErrFrame         Status = -4
	ErrParse         Status = -5
)

func (s Status) Error() string {
	switch s {
	case ErrInvalidArg:
		return "po32: invalid argument"
	case ErrRange:
		return "po32: value out of range"
	case ErrBufferTooSmall:
		return "po32: buffer too small"
	case ErrFrame:
		return "po32: frame error"
	case ErrParse:
		return "po32: parse error"
	default:
		return "po32: unknown error"
	}
}

func statusToError(s int) error {
	if s == 0 {
		return nil
	}
	return Status(s)
}
