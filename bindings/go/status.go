package libpo

// Status is a native libpo status code and implements error. Use Err to turn
// the success code into nil.
type Status int

const (
	OK                Status = 0
	ErrInvalidArg     Status = -1
	ErrRange          Status = -2
	ErrBufferTooSmall Status = -3
	ErrFrame          Status = -4
	ErrParse          Status = -5
)

func (s Status) Error() string {
	switch s {
	case OK:
		return "libpo: success"
	case ErrInvalidArg:
		return "libpo: invalid argument"
	case ErrRange:
		return "libpo: value out of range"
	case ErrBufferTooSmall:
		return "libpo: buffer too small"
	case ErrFrame:
		return "libpo: frame error"
	case ErrParse:
		return "libpo: parse error"
	default:
		return "libpo: unknown error"
	}
}

// Err converts OK to nil and every other status to an error.
func (s Status) Err() error {
	if s == OK {
		return nil
	}
	return s
}
