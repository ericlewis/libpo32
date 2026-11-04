/* Hosted PO-33/PO-133 export; wire transport and DSP live in the core. */
#include "po33.h"
#include "po_capture.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
typedef struct {
  FILE *payloads, *index;
  size_t count, bytes;
  int failed;
} dump_t;

static FILE *open_named(const char *directory, const char *name, const char *mode) {
  size_t a = strlen(directory), b = strlen(name);
  char *path;
  FILE *fp;
  if (a > SIZE_MAX - b - 2)
    return NULL;
  path = (char *)malloc(a + b + 2);
  if (!path)
    return NULL;
  memcpy(path, directory, a);
  path[a] = '/';
  memcpy(path + a + 1, name, b + 1);
  fp = fopen(path, mode);
  free(path);
  return fp;
}

static FILE *open_output(const char *directory, const char *name) {
  /* Exclusive creation also rejects duplicate selectors instead of silently
   * replacing an earlier section. Supported by the hosted macOS/Linux libc. */
  return open_named(directory, name, "wbx");
}

static int close_output(FILE **fp) {
  int result;
  if (!*fp)
    return 1;
  result = fclose(*fp);
  *fp = NULL;
  return result == 0;
}

static void store16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void store32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(value >> (8 * i));
}

/* The nine-byte metadata is preserved in sound-NNN.bin. Its trim/reference
 * semantics remain unresolved, so WAVs contain the entire following pool,
 * including allocation padding. RIFF's integer rate rounds 23437.5 to 23438. */
static int write_sound_wav(const char *directory, unsigned index, size_t raw_bytes) {
  char name[32];
  uint8_t header[44] = {0}, encoded[4096], pcm[8192];
  FILE *input = NULL, *output = NULL;
  size_t remaining;
  uint32_t pcm_bytes;
  int ok = 0;
  if (raw_bytes < 9 || raw_bytes - 9 > (UINT32_MAX - 36u) / 2u)
    return 0;
  if (raw_bytes == 9)
    return 1;
  remaining = raw_bytes - 9;
  pcm_bytes = (uint32_t)(remaining * 2u);
  snprintf(name, sizeof(name), "sound-%03u.bin", index);
  input = open_named(directory, name, "rb");
  if (!input || fread(encoded, 1, 9, input) != 9)
    goto cleanup;
  snprintf(name, sizeof(name), "sound-%03u.wav", index);
  output = open_output(directory, name);
  if (!output)
    goto cleanup;
  memcpy(header, "RIFF", 4);
  store32(header + 4, 36u + pcm_bytes);
  memcpy(header + 8, "WAVEfmt ", 8);
  store32(header + 16, 16);
  store16(header + 20, 1);
  store16(header + 22, 1);
  store32(header + 24, 23438);
  store32(header + 28, 23438u * 2u);
  store16(header + 32, 2);
  store16(header + 34, 16);
  memcpy(header + 36, "data", 4);
  store32(header + 40, pcm_bytes);
  if (fwrite(header, 1, sizeof(header), output) != sizeof(header))
    goto cleanup;
  while (remaining) {
    size_t count = remaining < sizeof(encoded) ? remaining : sizeof(encoded);
    if (fread(encoded, 1, count, input) != count)
      goto cleanup;
    for (size_t i = 0; i < count; ++i)
      store16(pcm + 2 * i, (uint16_t)po33_sample_decode(encoded[i]));
    if (fwrite(pcm, 2, count, output) != count)
      goto cleanup;
    remaining -= count;
  }
  if (fgetc(input) != EOF || ferror(input))
    goto cleanup;
  ok = 1;
cleanup:
  if (!close_output(&input))
    ok = 0;
  if (!close_output(&output))
    ok = 0;
  return ok;
}

typedef struct {
  dump_t raw;
  const char *directory;
  FILE *settings, *pattern, *sound;
  unsigned pattern_index, sound_index;
  int have_pattern, have_sound;
  size_t sound_bytes, pattern_count, sound_count, wav_count;
} semantic_dump_t;

static int finish_sound(semantic_dump_t *dump) {
  if (!dump->sound)
    return 1;
  if (!close_output(&dump->sound) ||
      !write_sound_wav(dump->directory, dump->sound_index, dump->sound_bytes))
    return 0;
  if (dump->sound_bytes > 9)
    ++dump->wav_count;
  return 1;
}

static int dump_packet(const po32_packet_t *packet, void *user) {
  semantic_dump_t *dump = (semantic_dump_t *)user;
  FILE *target = NULL;
  char name[32];
  if (fwrite(packet->payload, 1, packet->payload_len, dump->raw.payloads) != packet->payload_len ||
      fprintf(dump->raw.index, "%zu\t%zu\t%04x\t%zu\t%zu\n", dump->raw.count, packet->offset,
              packet->tag_code, packet->payload_len, dump->raw.bytes) < 0)
    goto failure;
  ++dump->raw.count;
  dump->raw.bytes += packet->payload_len;
  switch (packet->tag_code) {
  case PO33_TAG_SETTINGS:
    if (!dump->settings)
      dump->settings = open_output(dump->directory, "settings.bin");
    target = dump->settings;
    break;
  case PO33_TAG_PATTERN_SELECT:
    if (packet->payload_len != 1 || !close_output(&dump->pattern))
      goto failure;
    dump->pattern_index = packet->payload[0];
    dump->have_pattern = 1;
    return 0;
  case PO33_TAG_PATTERN_DATA:
    if (!dump->have_pattern)
      goto failure;
    if (!dump->pattern) {
      snprintf(name, sizeof(name), "pattern-%02u.bin", dump->pattern_index);
      dump->pattern = open_output(dump->directory, name);
      if (dump->pattern)
        ++dump->pattern_count;
    }
    target = dump->pattern;
    break;
  case PO33_TAG_SOUND_SELECT:
    if (packet->payload_len != 1 || !finish_sound(dump))
      goto failure;
    dump->sound_index = packet->payload[0];
    dump->sound_bytes = 0;
    dump->have_sound = 1;
    return 0;
  case PO33_TAG_SOUND_DATA:
    if (!dump->have_sound || packet->payload_len > SIZE_MAX - dump->sound_bytes)
      goto failure;
    if (!dump->sound) {
      snprintf(name, sizeof(name), "sound-%03u.bin", dump->sound_index);
      dump->sound = open_output(dump->directory, name);
      if (dump->sound)
        ++dump->sound_count;
    }
    target = dump->sound;
    dump->sound_bytes += packet->payload_len;
    break;
  default:
    return 0; /* Unknown packets remain available in the raw outputs. */
  }
  if (!target || fwrite(packet->payload, 1, packet->payload_len, target) != packet->payload_len)
    goto failure;
  return 0;
failure:
  dump->raw.failed = 1;
  return 1;
}

static int write_outputs(const char *directory, const uint8_t *frame, size_t frame_len,
                         uint32_t capture_rate) {
  po33_decode_result_t result = {0};
  po32_status_t status;
  semantic_dump_t dump = {0};
  FILE *fp = NULL;
  int ok = 0;
  /* No directory or artifact is created until the entire transfer validates. */
  status = po33_frame_parse(frame, frame_len, NULL, NULL, &result);
  if (status != PO32_OK || !result.done) {
    fprintf(stderr, "PO-33 frame validation failed (status %d, packets %zu).\n", (int)status,
            result.packet_count);
    return 0;
  }
  if (mkdir(directory, 0777) != 0) {
    fprintf(stderr, "Cannot create new output directory: %s\n", strerror(errno));
    return 0;
  }
  dump.directory = directory;
  fp = open_output(directory, "frame.bin");
  if (!fp || fwrite(frame, 1, result.frame_bytes, fp) != result.frame_bytes || !close_output(&fp))
    goto cleanup;
  dump.raw.payloads = open_output(directory, "payloads.bin");
  dump.raw.index = open_output(directory, "packets.tsv");
  if (!dump.raw.payloads || !dump.raw.index ||
      fprintf(dump.raw.index, "packet\tframe_offset\ttag\tpayload_length\tpayload_offset\n") < 0)
    goto cleanup;
  status = po33_frame_parse(frame, result.frame_bytes, dump_packet, &dump, &result);
  if (status != PO32_OK || dump.raw.failed || !result.done || !finish_sound(&dump))
    goto cleanup;
  /* Evaluate every close even when an earlier close fails. */
  {
    int a = close_output(&dump.raw.payloads), b = close_output(&dump.raw.index);
    int c = close_output(&dump.settings), d = close_output(&dump.pattern);
    if (!a || !b || !c || !d)
      goto cleanup;
  }
  fp = open_output(directory, "summary.txt");
  if (!fp ||
      fprintf(fp,
              "sample_rate=%u\nchannels=2\npackets=%zu\npayload_bytes=%zu\n"
              "frame_bytes=%zu\nfinal_state=%04x\ncomplete=%d\n"
              "pattern_files=%zu\nsound_files=%zu\nsound_wav_files=%zu\n"
              "sound_header_bytes=9\nsound_nominal_sample_rate=%.1f\n"
              "sound_wav_sample_rate=23438\nsound_wav_rate_rounded=1\n"
              "sound_trimmed=0\nsound_padding_preserved=1\n",
              capture_rate, dump.raw.count, dump.raw.bytes, result.frame_bytes, result.final_state,
              result.done, dump.pattern_count, dump.sound_count, dump.wav_count,
              (double)PO33_SAMPLE_RATE_HZ) < 0 ||
      !close_output(&fp))
    goto cleanup;
  printf("Validated %zu packets, %zu payload bytes, %zu frame bytes; wrote %zu sound WAVs.\n",
         dump.raw.count, dump.raw.bytes, result.frame_bytes, dump.wav_count);
  ok = 1;
cleanup:
  if (!ok)
    fprintf(stderr, "Failed to write decoded outputs.\n");
  close_output(&fp);
  close_output(&dump.raw.payloads);
  close_output(&dump.raw.index);
  close_output(&dump.settings);
  close_output(&dump.pattern);
  close_output(&dump.sound);
  return ok;
}

int main(int argc, char **argv) {
  po_capture_t capture = {NULL, 0, 0};
  uint8_t *frame = NULL;
  size_t frame_len = 0;
  int rc = 1;
  if (argc != 3) {
    fprintf(stderr, "Usage: %s input.wav new_output_directory\n", argv[0]);
    return 2;
  }
  if (!po_capture_read(argv[1], &capture)) {
    fprintf(stderr, "Expected finite stereo PCM16/24/32 or float32 WAV at 32–192 kHz.\n");
    goto cleanup;
  }
  if (!po_capture_decode(&capture, PO_STEREO_CHANNEL_NORMAL, &frame, &frame_len)) {
    fprintf(stderr, "No complete PO-33 counter preamble and sync recovered.\n");
    goto cleanup;
  }
  if (write_outputs(argv[2], frame, frame_len, capture.rate))
    rc = 0;
cleanup:
  free(capture.samples);
  free(frame);
  return rc;
}
