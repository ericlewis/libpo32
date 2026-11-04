#ifndef PO_COMMON_H
#define PO_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Historical tags and constants remain the canonical types for source and ABI
 * compatibility. Neutral aliases let the other model families share them. */
#define PO32_INITIAL_STATE    0x1D0Fu
#define PO32_MAX_PAYLOAD      255
#define PO32_FINAL_TAIL_BYTES 5

typedef enum po32_status {
  PO32_OK = 0,
  PO32_ERR_INVALID_ARG = -1,
  PO32_ERR_RANGE = -2,
  PO32_ERR_BUFFER_TOO_SMALL = -3,
  PO32_ERR_FRAME = -4,
  PO32_ERR_PARSE = -5
} po32_status_t;

typedef struct po32_packet_trailer {
  uint8_t lo, hi;
  uint16_t state;
  int matches_state;
} po32_packet_trailer_t;

typedef struct po32_packet {
  uint16_t tag_code;
  size_t offset;
  size_t payload_len;
  uint8_t payload[PO32_MAX_PAYLOAD]; /* inline bytes, retained when the packet struct is copied */
  po32_packet_trailer_t trailer;
} po32_packet_t;

typedef struct po32_final_tail {
  uint8_t marker_c3, marker_71;
  uint8_t special_byte;
  uint8_t final_lo, final_hi;
  uint16_t state_before_special;
  uint16_t state_after_special;
  uint16_t final_state;
} po32_final_tail_t;

/*
 * Packet callback for po32_frame_parse() and po32_demodulator_push(). The
 * packet pointer is borrowed for the duration of the callback. Copy the packet
 * struct, including its inline payload array, to retain its contents after the
 * callback returns. packet->offset is the packet's byte offset within the
 * full transmitted frame, counting the 128-byte preamble, from both callers
 * and from po32_builder_append_packet().
 * Return 0 to continue parsing, nonzero to stop early. Stopping is terminal
 * for the streaming demodulator — see po32_demodulator_push().
 */
typedef int (*po32_packet_callback_t)(const po32_packet_t *packet, void *user);

#define PO_INITIAL_STATE        PO32_INITIAL_STATE
#define PO_MAX_PAYLOAD          PO32_MAX_PAYLOAD
#define PO_FINAL_TAIL_BYTES     PO32_FINAL_TAIL_BYTES
#define PO_OK                   PO32_OK
#define PO_ERR_INVALID_ARG      PO32_ERR_INVALID_ARG
#define PO_ERR_RANGE            PO32_ERR_RANGE
#define PO_ERR_BUFFER_TOO_SMALL PO32_ERR_BUFFER_TOO_SMALL
#define PO_ERR_FRAME            PO32_ERR_FRAME
#define PO_ERR_PARSE            PO32_ERR_PARSE

typedef po32_status_t po_status_t;
typedef po32_packet_trailer_t po_packet_trailer_t;
typedef po32_packet_t po_packet_t;
typedef po32_final_tail_t po_final_tail_t;
/* Offsets include the model's entire preamble. Model parsers document their
 * completion and callback-stop behavior separately. */
typedef po32_packet_callback_t po_packet_callback_t;

#ifdef __cplusplus
}
#endif
#endif
