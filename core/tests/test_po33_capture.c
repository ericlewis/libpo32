#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include "wav_test_helpers.h"

#define main po33_capture_example_main
#include "../examples/po33_decode_capture.c"
#undef main
#define PI 3.14159265358979323846

/* A separate bit-at-a-time CCITT implementation builds the transmitted data.
 * The renderer below is an independent, continuous-time QPSK transmitter;
 * it does not call the decoder's packing, filtering, or timing helpers. */
static uint16_t reference_crc(uint16_t state, uint8_t byte) {
  state ^= (uint16_t)((uint16_t)byte << 8);
  for (unsigned i = 0; i < 8; ++i)
    state = (uint16_t)((state << 1) ^ ((state & 0x8000u) ? 0x1021u : 0));
  return state;
}

static void encoded_byte(uint8_t *wire, size_t *length, uint16_t *state, uint8_t byte) {
  wire[(*length)++] = (uint8_t)(byte + (uint8_t)*state);
  *state = reference_crc(*state, byte);
}

static void encoded_packet(uint8_t *wire, size_t *length, uint16_t *state, uint16_t tag,
                           const uint8_t *payload, size_t count) {
  uint16_t crc;
  encoded_byte(wire, length, state, (uint8_t)tag);
  encoded_byte(wire, length, state, (uint8_t)(tag >> 8));
  encoded_byte(wire, length, state, (uint8_t)count);
  for (size_t i = 0; i < count; ++i)
    encoded_byte(wire, length, state, payload[i]);
  crc = *state;
  encoded_byte(wire, length, state, (uint8_t)crc);
  encoded_byte(wire, length, state, (uint8_t)(crc >> 8));
}

static size_t fixture_frame(uint8_t *wire) {
  static const uint8_t sync[] = {0x4d, 0xb8, 0x16, 0x5d};
  static const uint8_t golden_begin[] = {0x6a, 0x9a, 0x34, 0xe2, 0x48};
  uint8_t payload[71];
  size_t length = 4096;
  uint16_t state = 0x1d0f;
  for (size_t i = 0; i < 4092; ++i)
    wire[i] = (uint8_t)i;
  memcpy(wire + 4092, sync, 4);
  encoded_packet(wire, &length, &state, 0x985b, NULL, 0);
  assert(!memcmp(wire + 4096, golden_begin, sizeof(golden_begin)));
  for (size_t i = 0; i < sizeof(payload); ++i)
    payload[i] = (uint8_t)(i * 29 + 7);
  encoded_packet(wire, &length, &state, 0x4321, payload, sizeof(payload));
  encoded_packet(wire, &length, &state, 0x826f, NULL, 0);
  return length;
}

static po_capture_t render_fixture(const uint8_t *wire, size_t length, uint32_t rate,
                                   double amplitude, int channel_mode) {
  const double baud = 7812.1, lead = 0.23, tail = 0.23;
  size_t symbols = length * 2 + 1;
  uint8_t *phase = (uint8_t *)calloc(2 * (symbols + 1), 1);
  po_capture_t capture = {NULL, 0, rate};
  assert(phase);
  for (size_t i = 0; i < length * 2; ++i) {
    unsigned nibble = (wire[i / 2] >> ((i & 1) * 4)) & 15;
    phase[2 * (i + 1)] = (uint8_t)((phase[2 * i] + (nibble >> 2)) & 3);
    phase[2 * (i + 1) + 1] = (uint8_t)((phase[2 * i + 1] + (nibble & 3)) & 3);
  }
  phase[2 * symbols] = phase[2 * (symbols - 1)];
  phase[2 * symbols + 1] = phase[2 * (symbols - 1) + 1];
  capture.frames = (size_t)((lead + symbols / baud + tail) * rate);
  capture.samples = (float *)calloc(capture.frames * 2, sizeof(float));
  assert(capture.samples);
  for (size_t i = 0; i < capture.frames; ++i) {
    double time = (double)i / rate - lead, position = time * baud;
    if (position >= 0 && position < (double)symbols) {
      size_t symbol = (size_t)position;
      double within = position - symbol;
      /* Change phase smoothly around the last quarter of each carrier cycle.
       * This is a transmitter pulse shape, not the receiver's sample rule. */
      double mix = within < 0.60 ? 0 : within > 0.90 ? 1 : (within - 0.60) / 0.30;
      for (size_t ch = 0; ch < 2; ++ch) {
        double a = sin(2 * PI * position + phase[2 * symbol + ch] * PI / 2);
        double b = sin(2 * PI * position + phase[2 * (symbol + 1) + ch] * PI / 2);
        capture.samples[2 * i + ch] = amplitude * ((1 - mix) * a + mix * b);
      }
    }
    if (channel_mode == 1) {
      double average = (capture.samples[2 * i] + capture.samples[2 * i + 1]) * 0.5;
      capture.samples[2 * i] = capture.samples[2 * i + 1] = average;
    } else if (channel_mode == 2) {
      double left = capture.samples[2 * i];
      capture.samples[2 * i] = capture.samples[2 * i + 1];
      capture.samples[2 * i + 1] = left;
    }
  }
  free(phase);
  return capture;
}

static void put16(FILE *fp, uint16_t value) {
  int status = fputc((int)(value & 255), fp);
  assert(status != EOF);
  status = fputc((int)(value >> 8), fp);
  assert(status != EOF);
}

static void put32(FILE *fp, uint32_t value) {
  put16(fp, (uint16_t)value);
  put16(fp, (uint16_t)(value >> 16));
}

static void save_fixture(const char *path, const po_capture_t *capture, int floating) {
  FILE *fp = fopen(path, "wb");
  uint32_t width = floating ? 4 : 2;
  uint32_t data_size = (uint32_t)(capture->frames * 2 * width);
  size_t written;
  int status;
  assert(fp);
  written = fwrite("RIFF", 1, 4, fp);
  assert(written == 4);
  put32(fp, 36 + data_size);
  written = fwrite("WAVEfmt ", 1, 8, fp);
  assert(written == 8);
  put32(fp, 16);
  put16(fp, (uint16_t)(floating ? 3 : 1));
  put16(fp, 2);
  put32(fp, capture->rate);
  put32(fp, capture->rate * 2 * width);
  put16(fp, (uint16_t)(2 * width));
  put16(fp, (uint16_t)(width * 8));
  written = fwrite("data", 1, 4, fp);
  assert(written == 4);
  put32(fp, data_size);
  for (size_t i = 0; i < capture->frames * 2; ++i) {
    if (floating) {
      float value = (float)capture->samples[i];
      uint32_t bits;
      memcpy(&bits, &value, sizeof(bits));
      put32(fp, bits);
    } else {
      long value = lround(capture->samples[i] * 32767);
      assert(value >= -32768 && value <= 32767);
      put16(fp, (uint16_t)value);
    }
  }
  status = fclose(fp);
  assert(status == 0);
}

/* FFmpeg emits WAVE_FORMAT_EXTENSIBLE even for ordinary stereo PCM24.
 * Convert the fixture's standard descriptor into the equivalent PCM subtype. */
static void make_extensible(const char *path) {
  uint8_t pcm_guid[16] = {1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
  FILE *fp = fopen(path, "rb");
  uint8_t *bytes;
  long size;
  size_t transferred;
  int status;
  assert(fp);
  status = fseek(fp, 0, SEEK_END);
  assert(status == 0);
  size = ftell(fp);
  assert(size >= 44);
  status = fseek(fp, 0, SEEK_SET);
  assert(status == 0);
  bytes = (uint8_t *)malloc((size_t)size);
  assert(bytes);
  transferred = fread(bytes, 1, (size_t)size, fp);
  assert(transferred == (size_t)size);
  status = fclose(fp);
  assert(status == 0);
  fp = fopen(path, "wb");
  assert(fp);
  transferred = fwrite(bytes, 1, 4, fp);
  assert(transferred == 4);
  put32(fp, (uint32_t)size - 8 + 24);
  transferred = fwrite(bytes + 8, 1, 8, fp);
  assert(transferred == 8);
  put32(fp, 40);
  put16(fp, 0xfffe);
  transferred = fwrite(bytes + 22, 1, 14, fp);
  assert(transferred == 14);
  put16(fp, 22);                                   /* cbSize */
  put16(fp, test_wav_u16(bytes + 34));             /* valid bits equal the container width */
  pcm_guid[0] = (uint8_t)test_wav_u16(bytes + 20); /* PCM or IEEE float subtype */
  put32(fp, 3);                                    /* front left and front right */
  transferred = fwrite(pcm_guid, 1, sizeof(pcm_guid), fp);
  assert(transferred == sizeof(pcm_guid));
  transferred = fwrite(bytes + 36, 1, (size_t)size - 36, fp);
  assert(transferred == (size_t)size - 36);
  status = fclose(fp);
  assert(status == 0);
  free(bytes);
}

static int validate_packet(const po32_packet_t *packet, void *user) {
  size_t *count = (size_t *)user;
  assert(packet->trailer.matches_state);
  if (*count == 0) {
    assert(packet->tag_code == 0x985b && packet->payload_len == 0);
  } else if (*count == 1) {
    assert(packet->tag_code == 0x4321 && packet->payload_len == 71);
    for (size_t i = 0; i < packet->payload_len; ++i)
      assert(packet->payload[i] == (uint8_t)(i * 29 + 7));
  } else {
    assert(*count == 2 && packet->tag_code == 0x826f && packet->payload_len == 0);
  }
  ++*count;
  return 0;
}

static int recover(po_capture_t *capture, const uint8_t *expected, size_t length) {
  uint8_t *frame = NULL;
  size_t frame_length = 0, packets = 0;
  po33_decode_result_t result = {0};
  int ok = po_capture_decode(capture, PO_STEREO_CHANNEL_NORMAL, &frame, &frame_length);
  if (ok) {
    ok = po33_frame_parse(frame, frame_length, expected ? validate_packet : NULL, &packets,
                          &result) == PO32_OK &&
         result.done;
    if (ok && expected) {
      assert(packets == 3 && result.packet_count == 3 && result.frame_bytes == length);
      assert(!memcmp(frame, expected, length));
    }
  }
  free(frame);
  return ok;
}

static void test_stereo_roundtrip(void) {
  const char *path = "po33_capture_synthetic.wav";
  uint8_t wire[8192];
  size_t length = fixture_frame(wire);
  int status;
  for (unsigned variant = 0; variant < 2; ++variant) {
    po_capture_t rendered =
        render_fixture(wire, length, variant ? 96000 : 44100, variant ? 0.015 : 0.60, 0);
    po_capture_t read = {0};
    save_fixture(path, &rendered, (int)variant);
    free(rendered.samples);
    status = po_capture_read(path, &read);
    assert(status);
    status = recover(&read, wire, length);
    assert(status);
    free(read.samples);
    {
      make_extensible(path);
      read = (po_capture_t){0};
      status = po_capture_read(path, &read);
      assert(status);
      status = recover(&read, wire, length);
      assert(status);
      free(read.samples);
    }
  }
  for (int mode = 1; mode <= 2; ++mode) {
    po_capture_t capture = render_fixture(wire, length, 44100, 0.60, mode);
    status = recover(&capture, NULL, 0);
    assert(!status);
    free(capture.samples);
  }
  /* Removing the final packet must not establish a complete backup. */
  {
    po_capture_t capture = render_fixture(wire, length - 5, 44100, 0.60, 0);
    status = recover(&capture, NULL, 0);
    assert(!status);
    free(capture.samples);
  }
  status = remove(path);
  assert(status == 0);
}

static void modify_u32(const char *path, long offset, uint32_t value) {
  FILE *fp = fopen(path, "r+b");
  int status;
  assert(fp);
  status = fseek(fp, offset, SEEK_SET);
  assert(status == 0);
  put32(fp, value);
  status = fclose(fp);
  assert(status == 0);
}

static void reject_file(const char *path) {
  po_capture_t capture = {0};
  int status = po_capture_read(path, &capture);
  assert(!status);
  free(capture.samples);
}

static void test_invalid_and_empty_captures(void) {
  const char *path = "po33_capture_invalid.wav";
  po_capture_t capture = {NULL, 44100, 44100};
  uint32_t random = 1;
  int status;
  capture.samples = (float *)calloc(capture.frames * 2, sizeof(float));
  assert(capture.samples);
  status = recover(&capture, NULL, 0);
  assert(!status); /* silence */
  for (size_t i = 0; i < capture.frames * 2; ++i) {
    random = random * 1664525u + 1013904223u;
    capture.samples[i] = ((double)(random >> 8) / 16777216.0 - 0.5) * 0.1;
  }
  status = recover(&capture, NULL, 0);
  assert(!status); /* deterministic broadband noise */
  save_fixture(path, &capture, 0);
  modify_u32(path, 4, 4); /* format/data exist only beyond the declared RIFF */
  reject_file(path);
  save_fixture(path, &capture, 0);
  modify_u32(path, 4, UINT32_MAX); /* outer RIFF chunk exceeds the file */
  reject_file(path);
  save_fixture(path, &capture, 0);
  modify_u32(path, 40, UINT32_MAX); /* data chunk extends past the file */
  reject_file(path);
  save_fixture(path, &capture, 0);
  modify_u32(path, 16, 8); /* incomplete format descriptor */
  reject_file(path);
  save_fixture(path, &capture, 0);
  modify_u32(path, 32, 0x00100002); /* PCM16 stereo block alignment is not 2 */
  reject_file(path);
  save_fixture(path, &capture, 0);
  modify_u32(path, 20, 0x00010001); /* mono PCM is not a backup's two lanes */
  reject_file(path);
  save_fixture(path, &capture, 1);
  modify_u32(path, 44, 0x7fc00000); /* NaN */
  reject_file(path);
  save_fixture(path, &capture, 1);
  modify_u32(path, 44, 0x7f800000); /* positive infinity */
  reject_file(path);
  /* Extensible descriptors must identify an exact supported subtype and
   * describe valid bits inside their declared container and chunk bounds. */
  {
    static const uint32_t invalid_extension[] = {
        0x00100014, /* cbSize too small for the subtype GUID */
        0x00100018, /* cbSize extends beyond the format chunk */
        0x00110016, /* 17 valid bits cannot fit a PCM16 container */
        0x00000016  /* zero valid bits */
    };
    for (size_t i = 0; i < sizeof(invalid_extension) / sizeof(invalid_extension[0]); ++i) {
      save_fixture(path, &capture, 0);
      make_extensible(path);
      modify_u32(path, 36, invalid_extension[i]);
      reject_file(path);
    }
  }
  save_fixture(path, &capture, 0);
  make_extensible(path);
  modify_u32(path, 60, 0); /* damaged GUID with a superficially valid PCM prefix */
  reject_file(path);
  save_fixture(path, &capture, 1);
  make_extensible(path);
  modify_u32(path, 36, 0x00100016); /* IEEE float32 requires 32 valid bits */
  reject_file(path);
  capture.frames = 100;
  save_fixture(path, &capture, 0);
  reject_file(path); /* too little audio to carry the counter preamble */
  {
    FILE *fp = fopen(path, "wb");
    size_t written;
    assert(fp);
    written = fwrite("RIFF", 1, 4, fp);
    assert(written == 4);
    status = fclose(fp);
    assert(status == 0);
    reject_file(path);
  }
  free(capture.samples);
  status = remove(path);
  assert(status == 0);
}

int main(void) {
  test_invalid_and_empty_captures();
  test_stereo_roundtrip();
  puts("PO-33 capture tests passed");
  return 0;
}
