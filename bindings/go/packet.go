package libpo

// MaxPayload is the maximum payload length of an ordinary wire packet.
const MaxPayload = 255

// PacketTrailer describes a packet's decoded checksum trailer.
type PacketTrailer struct {
	Lo, Hi       uint8
	State        uint16
	MatchesState bool
}

// Packet owns a decoded payload. Offset counts bytes from the start of the
// complete model-specific frame, including its preamble. A value copied from
// a callback remains valid after that callback returns.
type Packet struct {
	TagCode    uint16
	Offset     int
	PayloadLen int
	Payload    [MaxPayload]byte
	Trailer    PacketTrailer
}
