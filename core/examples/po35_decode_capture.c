/* Hosted PO-35/PO-137 recovery. Transport and speech DSP live in the core. */
#include "po35.h"
#include "po35_synth.h"
#include "po_capture.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
  int seen;
  po35_speech_header_t header;
  uint8_t header_bytes[PO35_SPEECH_HEADER_BYTES];
  uint8_t *records;
  size_t record_bytes;
  float *samples;
  size_t sample_count;
} d35_slot_t;

typedef struct {
  d35_slot_t slots[PO35_SPEECH_SLOT_COUNT];
  int active, failed, settings_seen;
  uint16_t patterns;
  size_t speech_count, pattern_count;
} d35_collection_t;

typedef struct {
  const char *directory;
  char names[80][32];
  size_t count;
} d35_outputs_t;

typedef struct {
  d35_outputs_t *outputs;
  FILE *payloads, *index;
  size_t packets, bytes;
  int failed;
} d35_dump_t;

static int d35_collect(const po_packet_t *packet, void *user) {
  d35_collection_t *collection = (d35_collection_t *)user;
  d35_slot_t *slot;
  if (collection->active >= 0) {
    switch (packet->tag_code) {
    case PO35_TAG_SPEECH_HEADER:
    case PO35_TAG_SETTINGS:
    case PO35_TAG_PATTERN:
    case PO35_TAG_PATTERN_EMPTY:
    case PO35_TAG_PATCH:
    case PO35_TAG_TABLE_7BE7:
    case PO35_TAG_TABLE_1C7F:
      goto failure;
    default:
      break; /* Unknown extensions may be interleaved; they stay raw. */
    }
  }
  switch (packet->tag_code) {
  case PO35_TAG_SPEECH_HEADER: {
    po35_speech_header_t header;
    size_t bytes;
    if (po35_speech_header_decode(packet->payload, packet->payload_len, &header) != PO_OK ||
        header.slot >= PO35_SPEECH_SLOT_COUNT || collection->slots[header.slot].seen)
      goto failure;
    slot = collection->slots + header.slot;
    bytes = (size_t)header.record_count * PO35_SPEECH_RECORD_BYTES;
    slot->records = bytes ? (uint8_t *)malloc(bytes) : NULL;
    if (bytes && slot->records == NULL)
      goto failure;
    slot->header = header;
    memcpy(slot->header_bytes, packet->payload, PO35_SPEECH_HEADER_BYTES);
    slot->seen = 1;
    collection->active = header.slot;
    ++collection->speech_count;
    break;
  }
  case PO35_TAG_SPEECH_DATA: {
    size_t expected;
    if (collection->active < 0 || packet->payload_len == 0)
      goto failure;
    slot = collection->slots + collection->active;
    expected = (size_t)slot->header.record_count * PO35_SPEECH_RECORD_BYTES;
    if (packet->payload_len > expected - slot->record_bytes)
      goto failure;
    memcpy(slot->records + slot->record_bytes, packet->payload, packet->payload_len);
    slot->record_bytes += packet->payload_len;
    break;
  }
  case PO35_TAG_SPEECH_END:
    if (collection->active < 0 || packet->payload_len != 0)
      goto failure;
    slot = collection->slots + collection->active;
    if (slot->record_bytes != (size_t)slot->header.record_count * PO35_SPEECH_RECORD_BYTES)
      goto failure;
    collection->active = -1;
    break;
  case PO35_TAG_SETTINGS: {
    po35_settings_t settings;
    if (collection->settings_seen ||
        po35_settings_decode(packet->payload, packet->payload_len, &settings) != PO_OK)
      goto failure;
    collection->settings_seen = 1;
    break;
  }
  case PO35_TAG_PATTERN:
  case PO35_TAG_PATTERN_EMPTY: {
    unsigned index;
    size_t expected = packet->tag_code == PO35_TAG_PATTERN ? PO35_PATTERN_BYTES : 1u;
    if (packet->payload_len != expected || packet->payload[0] >= 16)
      goto failure;
    index = packet->payload[0];
    if (collection->patterns & (uint16_t)(1u << index))
      goto failure;
    collection->patterns |= (uint16_t)(1u << index);
    ++collection->pattern_count;
    break;
  }
  default:
    break; /* Unknown packets survive in the raw frame and packet exports. */
  }
  return 0;
failure:
  collection->failed = 1;
  return 1;
}

static void d35_free_collection(d35_collection_t *collection) {
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i) {
    free(collection->slots[i].records);
    free(collection->slots[i].samples);
  }
}

static int d35_render(d35_collection_t *collection, po35_synth_bank_t bank, float **out_all,
                      size_t *out_count, uint32_t *out_rate) {
  po35_synth_t synth;
  size_t total = 0, position = 0, seen = 0, audio_count = 0;
  uint32_t rate = bank == PO35_SYNTH_BANK_HIGH ? 24000u : 8000u;
  float *all;
  if (po35_synth_init(&synth, bank, PO35_SYNTH_DEFAULT_SEED) != PO_OK)
    return 0;
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i) {
    d35_slot_t *slot = collection->slots + i;
    size_t rendered = 0;
    double peak = 0.0;
    if (!slot->seen)
      continue;
    if (po35_synth_samples_for_records(&synth, slot->header.record_count,
                                       slot->header.records_per_second,
                                       &slot->sample_count) != PO_OK ||
        slot->sample_count > SIZE_MAX / sizeof(float))
      return 0;
    if (!slot->sample_count)
      continue;
    slot->samples = (float *)malloc(slot->sample_count * sizeof(float));
    if (slot->samples == NULL ||
        po35_synth_render(&synth, slot->records, slot->record_bytes,
                          slot->header.records_per_second, slot->samples, slot->sample_count,
                          &rendered) != PO_OK ||
        rendered != slot->sample_count)
      return 0;
    for (size_t j = 0; j < rendered; ++j) {
      double magnitude = fabs((double)slot->samples[j]);
      if (!isfinite(magnitude))
        return 0;
      if (magnitude > peak)
        peak = magnitude;
    }
    if (peak > 0.0)
      for (size_t j = 0; j < rendered; ++j)
        slot->samples[j] = (float)((double)slot->samples[j] * (0.8 / peak));
    if (rendered > SIZE_MAX - total)
      return 0;
    total += rendered;
    ++audio_count;
  }
  if (audio_count > 1) {
    size_t gaps = (audio_count - 1) * (rate / 4u);
    if (gaps > SIZE_MAX - total)
      return 0;
    total += gaps;
  }
  if (total > SIZE_MAX / sizeof(float) || total > (UINT32_MAX - 36u) / 2u)
    return 0;
  all = total ? (float *)calloc(total, sizeof(float)) : NULL;
  if (total && all == NULL)
    return 0;
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i) {
    d35_slot_t *slot = collection->slots + i;
    if (!slot->sample_count)
      continue;
    if (seen++)
      position += rate / 4u;
    memcpy(all + position, slot->samples, slot->sample_count * sizeof(float));
    position += slot->sample_count;
  }
  *out_all = all;
  *out_count = total;
  *out_rate = rate;
  return 1;
}

static char *d35_path(const char *directory, const char *name) {
  size_t a = strlen(directory), b = strlen(name);
  char *path;
  if (a > SIZE_MAX - b - 2u)
    return NULL;
  path = (char *)malloc(a + b + 2u);
  if (path != NULL) {
    memcpy(path, directory, a);
    path[a] = '/';
    memcpy(path + a + 1, name, b + 1);
  }
  return path;
}

static FILE *d35_open(d35_outputs_t *outputs, const char *name) {
  char *path;
  FILE *fp;
  if (outputs->count >= 80 || strlen(name) >= sizeof(outputs->names[0]))
    return NULL;
  path = d35_path(outputs->directory, name);
  if (path == NULL)
    return NULL;
  fp = fopen(path, "wbx");
  free(path);
  if (fp != NULL)
    strcpy(outputs->names[outputs->count++], name);
  return fp;
}

static int d35_close(FILE **fp) {
  int status;
  if (*fp == NULL)
    return 1;
  status = fclose(*fp);
  *fp = NULL;
  return status == 0;
}

static void d35_remove_outputs(d35_outputs_t *outputs) {
  for (size_t i = 0; i < outputs->count; ++i) {
    char *path = d35_path(outputs->directory, outputs->names[i]);
    if (path != NULL) {
      remove(path);
      free(path);
    }
  }
  rmdir(outputs->directory);
}

static int d35_write_bytes(d35_outputs_t *outputs, const char *name, const uint8_t *data,
                           size_t count) {
  FILE *fp = d35_open(outputs, name);
  int ok;
  if (fp == NULL)
    return 0;
  ok = count == 0 || fwrite(data, 1, count, fp) == count;
  if (!d35_close(&fp))
    ok = 0;
  return ok;
}

static void d35_store16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}
static void d35_store32(uint8_t *out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out[i] = (uint8_t)(value >> (8u * i));
}

static int d35_write_wav(d35_outputs_t *outputs, const char *name, const float *samples,
                         size_t count, uint32_t rate) {
  uint8_t header[44] = {0}, pcm[4096];
  FILE *fp = NULL;
  int ok = 0;
  if (count > (UINT32_MAX - 36u) / 2u)
    return 0;
  memcpy(header, "RIFF", 4);
  d35_store32(header + 4, 36u + (uint32_t)count * 2u);
  memcpy(header + 8, "WAVEfmt ", 8);
  d35_store32(header + 16, 16);
  d35_store16(header + 20, 1);
  d35_store16(header + 22, 1);
  d35_store32(header + 24, rate);
  d35_store32(header + 28, rate * 2u);
  d35_store16(header + 32, 2);
  d35_store16(header + 34, 16);
  memcpy(header + 36, "data", 4);
  d35_store32(header + 40, (uint32_t)count * 2u);
  fp = d35_open(outputs, name);
  if (fp == NULL || fwrite(header, 1, sizeof(header), fp) != sizeof(header))
    goto cleanup;
  for (size_t position = 0; position < count;) {
    size_t chunk = count - position;
    if (chunk > sizeof(pcm) / 2u)
      chunk = sizeof(pcm) / 2u;
    for (size_t i = 0; i < chunk; ++i) {
      double value = (double)samples[position + i] * 32767.0;
      int16_t sample;
      if (!isfinite(value) || value < -32768.0 || value > 32767.0)
        goto cleanup;
      sample = (int16_t)(value < 0 ? value - 0.5 : value + 0.5);
      d35_store16(pcm + 2u * i, (uint16_t)sample);
    }
    if (fwrite(pcm, 2, chunk, fp) != chunk)
      goto cleanup;
    position += chunk;
  }
  ok = 1;
cleanup:
  if (!d35_close(&fp))
    ok = 0;
  return ok;
}

static int d35_dump_packet(const po_packet_t *packet, void *user) {
  d35_dump_t *dump = (d35_dump_t *)user;
  char name[32];
  if (fwrite(packet->payload, 1, packet->payload_len, dump->payloads) != packet->payload_len ||
      fprintf(dump->index, "%zu\t%zu\t%04x\t%zu\t%zu\n", dump->packets, packet->offset,
              (unsigned)packet->tag_code, packet->payload_len, dump->bytes) < 0)
    goto failure;
  ++dump->packets;
  dump->bytes += packet->payload_len;
  if (packet->tag_code == PO35_TAG_SETTINGS) {
    if (!d35_write_bytes(dump->outputs, "settings.bin", packet->payload, packet->payload_len))
      goto failure;
  } else if (packet->tag_code == PO35_TAG_PATTERN || packet->tag_code == PO35_TAG_PATTERN_EMPTY) {
    snprintf(name, sizeof(name), "pattern-%02u.bin", (unsigned)packet->payload[0]);
    if (!d35_write_bytes(dump->outputs, name, packet->payload, packet->payload_len))
      goto failure;
  }
  return 0;
failure:
  dump->failed = 1;
  return 1;
}

static int d35_write_outputs(const char *directory, const uint8_t *frame, size_t frame_len,
                             uint32_t capture_rate, po_stereo_channel_order_t order,
                             po35_synth_bank_t bank) {
  d35_collection_t collection = {0};
  d35_outputs_t outputs = {0};
  d35_dump_t dump = {0};
  po35_decode_result_t result = {0};
  float *all = NULL;
  size_t all_count = 0, audio_files = 0;
  uint32_t speech_rate = 0;
  FILE *summary = NULL;
  int made_directory = 0, ok = 0;
  collection.active = -1;
  outputs.directory = directory;
  dump.outputs = &outputs;
  if (po35_frame_parse(frame, frame_len, d35_collect, &collection, &result) != PO_OK ||
      !result.done || result.stopped || collection.failed || collection.active >= 0) {
    fprintf(stderr,
            "Invalid PO-35 frame or incomplete/duplicate speech, settings, or pattern group.\n");
    goto cleanup;
  }
  frame_len = result.frame_bytes;
  if (!d35_render(&collection, bank, &all, &all_count, &speech_rate)) {
    fprintf(stderr, "Speech rendering failed; no output directory was created.\n");
    goto cleanup;
  }
  if (mkdir(directory, 0777) != 0) {
    fprintf(stderr, "Cannot create new output directory: %s\n", strerror(errno));
    goto cleanup;
  }
  made_directory = 1;
  if (!d35_write_bytes(&outputs, "frame.bin", frame, frame_len))
    goto cleanup;
  dump.payloads = d35_open(&outputs, "payloads.bin");
  dump.index = d35_open(&outputs, "packets.tsv");
  if (!dump.payloads || !dump.index ||
      fprintf(dump.index, "packet\tframe_offset\ttag\tpayload_length\tpayload_offset\n") < 0 ||
      po35_frame_parse(frame, frame_len, d35_dump_packet, &dump, &result) != PO_OK ||
      !result.done || dump.failed)
    goto cleanup;
  {
    int a = d35_close(&dump.payloads), b = d35_close(&dump.index);
    if (!a || !b)
      goto cleanup;
  }
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i) {
    d35_slot_t *slot = collection.slots + i;
    char name[32];
    if (!slot->seen)
      continue;
    snprintf(name, sizeof(name), "speech-%02u-header.bin", (unsigned)i);
    if (!d35_write_bytes(&outputs, name, slot->header_bytes, PO35_SPEECH_HEADER_BYTES))
      goto cleanup;
    snprintf(name, sizeof(name), "speech-%02u.bin", (unsigned)i);
    if (!d35_write_bytes(&outputs, name, slot->records, slot->record_bytes))
      goto cleanup;
    if (!slot->sample_count)
      continue;
    snprintf(name, sizeof(name), "speech-%02u.wav", (unsigned)i);
    if (!d35_write_wav(&outputs, name, slot->samples, slot->sample_count, speech_rate))
      goto cleanup;
    ++audio_files;
  }
  if (all_count && !d35_write_wav(&outputs, "all-speech.wav", all, all_count, speech_rate))
    goto cleanup;
  summary = d35_open(&outputs, "summary.txt");
  if (summary == NULL ||
      fprintf(summary,
              "complete=1\nframe_bytes=%zu\npackets=%zu\npayload_bytes=%zu\nfinal_state=%04x\n"
              "capture_sample_rate=%u\nchannels=2\nchannel_order=%s\n"
              "speech_groups=%zu\nspeech_audio_files=%zu\npattern_files=%zu\n"
              "synth_model_version=%d\napproximate=1\n"
              "speech_bank=%s\nspeech_sample_rate=%u\nspeech_seed=%u\n"
              "noise=xorshift32-Marsaglia-Gaussian\n"
              "normalization=per-slot-peak\nnormalization_peak=0.8\n"
              "concatenation=ascending-slot-index\nconcatenation_gap_seconds=0.25\n"
              "hardware_audio_equivalence=unverified\n",
              frame_len, dump.packets, dump.bytes, (unsigned)result.final_state, capture_rate,
              order == PO_STEREO_CHANNEL_SWAPPED ? "swapped" : "normal", collection.speech_count,
              audio_files, collection.pattern_count, PO35_SYNTH_MODEL_VERSION,
              bank == PO35_SYNTH_BANK_HIGH ? "high" : "low", speech_rate,
              (unsigned)PO35_SYNTH_DEFAULT_SEED) < 0)
    goto cleanup;
  for (size_t i = 0; i < PO35_SPEECH_SLOT_COUNT; ++i) {
    const d35_slot_t *slot = collection.slots + i;
    if (slot->seen &&
        fprintf(summary,
                "speech_%02u_records=%u\nspeech_%02u_records_per_second=%u\n"
                "speech_%02u_samples=%zu\n",
                (unsigned)i, (unsigned)slot->header.record_count, (unsigned)i,
                (unsigned)slot->header.records_per_second, (unsigned)i, slot->sample_count) < 0)
      goto cleanup;
  }
  if (!d35_close(&summary))
    goto cleanup;
  printf("PO-35 complete: %zu packets, %zu speech groups, channel order %s.\n", dump.packets,
         collection.speech_count, order == PO_STEREO_CHANNEL_SWAPPED ? "swapped" : "normal");
  ok = 1;
cleanup:
  d35_close(&summary);
  d35_close(&dump.payloads);
  d35_close(&dump.index);
  if (!ok && made_directory) {
    fprintf(stderr, "Export failed; removing files created by this attempt.\n");
    d35_remove_outputs(&outputs);
  }
  free(all);
  d35_free_collection(&collection);
  return ok;
}

static void d35_usage(const char *program) {
  fprintf(stderr,
          "Usage: %s input.wav new-output-directory [--swap] [--bank high|low]\n"
          "Default: auto channel order, high bank (24 kHz), seed 350.\n"
          "Writes validated raw packets and approximate, peak-normalized speech previews.\n",
          program);
}

int main(int argc, char **argv) {
  po_capture_t capture = {0};
  po35_synth_bank_t bank = PO35_SYNTH_BANK_HIGH;
  po_stereo_channel_order_t order = PO_STEREO_CHANNEL_NORMAL;
  uint8_t *wire = NULL;
  size_t length = 0;
  int force_swap = 0, bank_seen = 0, found = 0, ok = 0;
  if (argc < 3) {
    d35_usage(argv[0]);
    return 2;
  }
  for (int i = 3; i < argc; ++i) {
    if (!strcmp(argv[i], "--swap") && !force_swap) {
      force_swap = 1;
    } else if (!strcmp(argv[i], "--bank") && !bank_seen && i + 1 < argc) {
      ++i;
      if (!strcmp(argv[i], "high"))
        bank = PO35_SYNTH_BANK_HIGH;
      else if (!strcmp(argv[i], "low"))
        bank = PO35_SYNTH_BANK_LOW;
      else {
        d35_usage(argv[0]);
        return 2;
      }
      bank_seen = 1;
    } else {
      d35_usage(argv[0]);
      return 2;
    }
  }
  if (!po_capture_read(argv[1], &capture)) {
    fprintf(stderr, "Cannot read supported stereo WAV capture.\n");
    return 1;
  }
  for (unsigned attempt = force_swap ? 1u : 0u; attempt < 2u; ++attempt) {
    po35_decode_result_t result;
    order = attempt ? PO_STEREO_CHANNEL_SWAPPED : PO_STEREO_CHANNEL_NORMAL;
    if (po_capture_decode(&capture, order, &wire, &length) &&
        po35_frame_parse(wire, length, NULL, NULL, &result) == PO_OK && result.done) {
      found = 1;
      length = result.frame_bytes;
      break;
    }
    free(wire);
    wire = NULL;
  }
  if (found)
    ok = d35_write_outputs(argv[2], wire, length, capture.rate, order, bank);
  else
    fprintf(stderr, "No complete CRC-valid PO-35 frame in the requested channel order(s).\n");
  free(wire);
  free(capture.samples);
  return ok ? 0 : 1;
}
