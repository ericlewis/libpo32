package po32

import "github.com/ericlewis/libpo32/bindings/go"

// Status is the shared libpo status error type.
type Status = libpo.Status

const (
	ErrInvalidArg     = libpo.ErrInvalidArg
	ErrRange          = libpo.ErrRange
	ErrBufferTooSmall = libpo.ErrBufferTooSmall
	ErrFrame          = libpo.ErrFrame
	ErrParse          = libpo.ErrParse
)

func statusToError(s int) error {
	return Status(s).Err()
}
