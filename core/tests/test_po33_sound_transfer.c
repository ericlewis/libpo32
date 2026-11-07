#ifdef NDEBUG
#undef NDEBUG
#endif
#include "po33.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct sound_check {
  const uint8_t *samples;
  size_t count, allocation, position, packets;
  uint8_t slot;
} sound_check_t;

static int check_packet(const po_packet_t *packet, void *user) {
  sound_check_t *check = user;
  if (check->packets == 0u)
    assert(packet->tag_code == PO33_TAG_BEGIN);
  else if (check->packets == 1u) {
    assert(packet->tag_code == PO33_TAG_SOUND_SELECT && packet->payload_len == 1u);
    assert(packet->payload[0] == check->slot);
  } else if (packet->tag_code == PO33_TAG_END) {
    assert(check->position == PO33_SOUND_HEADER_BYTES + check->allocation);
  } else {
    size_t boundary;
    assert(packet->tag_code == PO33_TAG_SOUND_DATA);
    if (check->position == 0u) {
      po33_sound_header_t header;
      assert(packet->payload_len >= PO33_SOUND_HEADER_BYTES);
      assert(po33_sound_header_decode(packet->payload, PO33_SOUND_HEADER_BYTES, &header) == PO_OK);
      assert(header.source_selector == check->slot && header.range_start_raw == 0u);
      assert(header.range_end_raw == check->count);
    }
    boundary = check->position < PO33_SOUND_HEADER_BYTES
                   ? PO33_SOUND_HEADER_BYTES + 4096u
                   : PO33_SOUND_HEADER_BYTES +
                         ((check->position - PO33_SOUND_HEADER_BYTES) / 4096u + 1u) * 4096u;
    assert(check->position + packet->payload_len <= boundary);
    for (size_t i = 0u; i < packet->payload_len; ++i) {
      size_t offset = check->position + i;
      if (offset >= PO33_SOUND_HEADER_BYTES) {
        offset -= PO33_SOUND_HEADER_BYTES;
        assert(packet->payload[i] == (offset < check->count ? check->samples[offset] : 0u));
      }
    }
    check->position += packet->payload_len;
  }
  ++check->packets;
  return 0;
}

static void test_layout(void) {
  uint8_t samples[8192], out[20000], seed[8192], staged[20000], inserted[20000];
  const size_t lengths[] = {1u, 255u, 4095u, 4096u, 4097u, 8192u};
  for (size_t i = 0u; i < sizeof(samples); ++i)
    samples[i] = (uint8_t)(i * 13u);
  for (uint8_t slot = 0u; slot < 8u; ++slot) {
    for (size_t i = 0u; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
      size_t count = lengths[i], bytes, allocation, written, seed_bytes, rebuilt;
      po33_builder_t builder;
      po33_decode_result_t parsed;
      po33_sample_insert_plan_t plan;
      assert(po33_sound_transfer_requirements(count, &bytes, &allocation) == PO_OK);
      assert(allocation == (count / 4096u + 1u) * 4096u);
      assert(bytes < sizeof(out));
      memset(out, 0xa5, sizeof(out));
      assert(po33_sound_transfer_build(slot, samples, count, out, bytes, &written) == PO_OK);
      assert(written == bytes && out[bytes] == 0xa5);
      sound_check_t check = {samples, count, allocation, 0u, 0u, slot};
      assert(po33_frame_parse(out, written, check_packet, &check, &parsed) == PO_OK);
      assert(parsed.done && !parsed.stopped && parsed.frame_bytes == written);
      assert(check.packets == 3u + 17u * allocation / 4096u);

      /* Equivalent insertion into an empty frame must produce identical bytes. */
      assert(po33_builder_init(&builder, seed, sizeof(seed)) == PO_OK);
      assert(po33_builder_append_packet(&builder, PO33_TAG_SOUND_SELECT, &slot, 1u, NULL) == PO_OK);
      assert(po33_builder_finish(&builder, &seed_bytes) == PO_OK);
      assert(po33_sample_insert_plan(seed, seed_bytes, slot, samples, count, &plan) == PO_OK);
      assert(po33_sample_insert_rebuild(seed, seed_bytes, slot, samples, count, &plan, staged,
                                        sizeof(staged), inserted, sizeof(inserted),
                                        &rebuilt) == PO_OK);
      assert(rebuilt == written && memcmp(inserted, out, written) == 0);
    }
  }
}

static void test_failures(void) {
  uint8_t sample = 1u, out[9000], before[9000];
  size_t bytes = 9u, allocation = 9u, written = 9u;
  memset(out, 0xa5, sizeof(out));
  memcpy(before, out, sizeof(out));
  assert(po33_sound_transfer_requirements(0u, &bytes, &allocation) == PO_ERR_RANGE);
  assert(bytes == 0u && allocation == 0u);
  assert(po33_sound_transfer_requirements(SIZE_MAX, &bytes, &allocation) == PO_ERR_RANGE);
  assert(po33_sound_transfer_requirements(1u, &bytes, &bytes) == PO_ERR_INVALID_ARG);
  assert(po33_sound_transfer_requirements(1u, NULL, &allocation) == PO_ERR_INVALID_ARG);
  assert(po33_sound_transfer_requirements(1u, &bytes, NULL) == PO_ERR_INVALID_ARG);
  assert(po33_sound_transfer_build(8u, &sample, 1u, out, sizeof(out), &written) == PO_ERR_RANGE);
  assert(written == 0u);
  assert(po33_sound_transfer_build(0u, &sample, 0u, out, sizeof(out), &written) == PO_ERR_RANGE);
  assert(po33_sound_transfer_build(0u, NULL, 1u, out, sizeof(out), &written) == PO_ERR_INVALID_ARG);
  assert(po33_sound_transfer_build(0u, &sample, 1u, NULL, sizeof(out), &written) ==
         PO_ERR_INVALID_ARG);
  assert(po33_sound_transfer_build(0u, &sample, 1u, out, sizeof(out), NULL) == PO_ERR_INVALID_ARG);
  assert(po33_sound_transfer_requirements(1u, &bytes, &allocation) == PO_OK);
  assert(po33_sound_transfer_build(0u, &sample, 1u, out, bytes - 1u, &written) ==
         PO_ERR_BUFFER_TOO_SMALL);
  assert(written == 0u && memcmp(out, before, sizeof(out)) == 0);
  assert(po33_sound_transfer_build(0u, out, 1u, out, sizeof(out), &written) == PO_ERR_INVALID_ARG);
  assert(po33_sound_transfer_build(0u, (const uint8_t *)&written, 1u, out, sizeof(out), &written) ==
         PO_ERR_INVALID_ARG);
  assert(po33_sound_transfer_build(0u, &sample, 1u, (uint8_t *)&written, sizeof(written),
                                   &written) == PO_ERR_INVALID_ARG);
}

int main(void) {
  test_layout();
  test_failures();
  puts("PO-33 standalone sound transfer tests passed");
  return 0;
}
