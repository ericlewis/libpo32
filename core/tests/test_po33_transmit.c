#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include "wav_test_helpers.h"
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../examples/po_stereo_wav.h"
#define main po33_receiver_example_main
#include "../examples/po33_decode_capture.c"
#undef main

/* Independent bitwise CRC fixture: no transmitter or core builder helpers. */
static uint16_t transmit_reference_crc(uint16_t state, uint8_t byte) {
  state ^= (uint16_t)((uint16_t)byte << 8);
  for (unsigned bit = 0; bit < 8; ++bit)
    state = (uint16_t)((state << 1) ^ ((state & 0x8000u) ? 0x1021u : 0u));
  return state;
}

static void fixture_byte(uint8_t *frame, size_t *length, uint16_t *state, uint8_t byte) {
  frame[(*length)++] = (uint8_t)(byte + (uint8_t)*state);
  *state = transmit_reference_crc(*state, byte);
}

static void fixture_packet(uint8_t *frame, size_t *length, uint16_t *state, uint16_t tag,
                           unsigned payload_length, unsigned sequence) {
  uint16_t checksum;
  fixture_byte(frame, length, state, (uint8_t)tag);
  fixture_byte(frame, length, state, (uint8_t)(tag >> 8));
  fixture_byte(frame, length, state, (uint8_t)payload_length);
  for (unsigned i = 0; i < payload_length; ++i)
    fixture_byte(frame, length, state, (uint8_t)(i * 37u + sequence * 19u));
  checksum = *state;
  fixture_byte(frame, length, state, (uint8_t)checksum);
  fixture_byte(frame, length, state, (uint8_t)(checksum >> 8));
}

static size_t make_transmit_fixture(uint8_t *frame) {
  uint16_t state = 0x1d0f;
  size_t length = 4096;
  static const uint8_t sync[4] = {0x4d, 0xb8, 0x16, 0x5d};
  static const uint8_t begin[5] = {0x6a, 0x9a, 0x34, 0xe2, 0x48};
  for (size_t i = 0; i < 4092; ++i)
    frame[i] = (uint8_t)i;
  memcpy(frame + 4092, sync, sizeof(sync));
  fixture_packet(frame, &length, &state, 0x985b, 0, 0);
  assert(!memcmp(frame + 4096, begin, sizeof(begin)));
  /* Varying packet sizes place payload and symbol boundaries throughout many
   * output buffers, including the noninteger 48 kHz samples/symbol ratio. */
  for (unsigned packet = 0; packet < 37; ++packet)
    fixture_packet(frame, &length, &state, 0x4321, packet % 3 ? 255 : 17, packet);
  fixture_packet(frame, &length, &state, 0x826f, 0, 0);
  return length;
}

static int check_transmitted_packet(const po32_packet_t *packet, void *user) {
  size_t *index = (size_t *)user;
  if (*index == 0 || *index == 38) {
    assert(packet->tag_code == (*index ? 0x826f : 0x985b));
    assert(packet->payload_len == 0);
  } else {
    unsigned sequence = (unsigned)*index - 1;
    assert(packet->tag_code == 0x4321);
    assert(packet->payload_len == (sequence % 3 ? 255 : 17));
    for (size_t i = 0; i < packet->payload_len; ++i)
      assert(packet->payload[i] == (uint8_t)(i * 37u + sequence * 19u));
  }
  ++*index;
  return 0;
}

static void test_transmit_roundtrip(void) {
  static const uint32_t rates[] = {44100, 48000, 96000};
  const char *path = "po33_transmit_roundtrip.wav";
  uint8_t frame[16384];
  size_t length = make_transmit_fixture(frame);
  for (size_t variant = 0; variant < sizeof(rates) / sizeof(rates[0]); ++variant) {
    po_capture_t capture = {0};
    uint8_t *decoded = NULL;
    size_t decoded_length = 0, packet_count = 0;
    po33_decode_result_t result = {0};
    int status;
    status = po33_write_transfer_wav(path, frame, length, rates[variant]);
    assert(status);
    status = po_capture_read(path, &capture);
    assert(status);
    assert(capture.rate == rates[variant]);
    for (size_t i = 0; i < capture.frames * 2; ++i)
      assert(fabs(capture.samples[i]) <= 0.60001);
    for (size_t i = 0; i < (size_t)rates[variant] / 4; ++i) {
      assert(capture.samples[2 * i] == 0 && capture.samples[2 * i + 1] == 0);
      assert(capture.samples[2 * (capture.frames - 1 - i)] == 0);
      assert(capture.samples[2 * (capture.frames - 1 - i) + 1] == 0);
    }
    status = po_capture_decode(&capture, PO_STEREO_CHANNEL_NORMAL, &decoded, &decoded_length);
    assert(status);
    assert(po33_frame_parse(decoded, decoded_length, check_transmitted_packet, &packet_count,
                            &result) == PO32_OK);
    assert(result.done && result.frame_bytes == length && packet_count == 39);
    assert(!memcmp(decoded, frame, length));
    free(capture.samples);
    free(decoded);
    status = remove(path);
    assert(status == 0);
  }
}

static void assert_absent(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (fp)
    fclose(fp);
  assert(!fp);
}

static void test_transmit_rejections(void) {
  const char *path = "po33_transmit_rejected.wav";
  const char *existing = "po33_transmit_existing.wav";
  uint8_t frame[16384], sentinel[] = {3, 1, 4, 1, 5, 9};
  size_t length = make_transmit_fixture(frame);
  FILE *fp;
  uint8_t actual[sizeof(sentinel)];
  int status;
  size_t transferred;
  status = po33_write_transfer_wav(NULL, frame, length, 44100);
  assert(!status);
  status = po33_write_transfer_wav(path, NULL, length, 44100);
  assert(!status);
  status = po33_write_transfer_wav(path, frame, 0, 44100);
  assert(!status);
  status = po33_write_transfer_wav(path, frame, SIZE_MAX, 44100);
  assert(!status);
  status = po33_write_transfer_wav(path, frame, length, 0);
  assert(!status);
  status = po33_write_transfer_wav(path, frame, length, 22050);
  assert(!status);
  status = po33_write_transfer_wav(path, frame, length, 48001);
  assert(!status);
  status = po33_write_transfer_wav(path, frame, length - 1, 44100);
  assert(!status);
  frame[5000] ^= 1;
  status = po33_write_transfer_wav(path, frame, length, 44100);
  assert(!status);
  frame[5000] ^= 1;
  frame[length] = 0;
  status = po33_write_transfer_wav(path, frame, length + 1, 44100);
  assert(!status);
  assert_absent(path);
  fp = fopen(existing, "wb");
  assert(fp);
  transferred = fwrite(sentinel, 1, sizeof(sentinel), fp);
  assert(transferred == sizeof(sentinel));
  status = fclose(fp);
  assert(status == 0);
  status = po33_write_transfer_wav(existing, frame, length, 48000);
  assert(!status);
  fp = fopen(existing, "rb");
  assert(fp);
  transferred = fread(actual, 1, sizeof(actual), fp);
  assert(transferred == sizeof(actual));
  status = fgetc(fp);
  assert(status == EOF);
  status = fclose(fp);
  assert(status == 0 && !memcmp(actual, sentinel, sizeof(actual)));
  status = remove(existing);
  assert(status == 0);
  status = po33_write_transfer_wav("po33-no-such-directory/out.wav", frame, length, 48000);
  assert(!status);
}

static void test_write_failure_removes_partial_file(void) {
  const char *path = "po33_transmit_partial.wav";
  pid_t child = fork();
  pid_t waited;
  int status;
  assert(child >= 0);
  if (child == 0) {
    uint8_t frame[16384];
    size_t length = make_transmit_fixture(frame);
    struct rlimit limit = {1024, 1024};
    void (*handler)(int);
    status = setrlimit(RLIMIT_FSIZE, &limit);
    assert(status == 0);
    handler = signal(SIGXFSZ, SIG_IGN);
    assert(handler != SIG_ERR);
    status = po33_write_transfer_wav(path, frame, length, 48000);
    assert(!status);
    assert_absent(path);
    _exit(0);
  }
  waited = waitpid(child, &status, 0);
  assert(waited == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  assert_absent(path);
}

int main(void) {
  test_transmit_rejections();
  test_transmit_roundtrip();
  test_write_failure_removes_partial_file();
  puts("PO-33 transmit tests passed");
  return 0;
}
