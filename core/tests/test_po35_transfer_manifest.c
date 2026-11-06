#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "po35.h"
#include "po_frame_report.h"

#define main po35_transfer_manifest_main
#include "../examples/po35_transfer_manifest.c"
#undef main

static void manifest_save(const char *path, const uint8_t *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  size_t written;
  int status;
  assert(file != NULL);
  written = fwrite(bytes, 1u, length, file);
  status = fclose(file);
  assert(written == length && status == 0);
}

static void manifest_append(po35_builder_t *builder, uint16_t tag_code, const uint8_t *payload,
                            size_t payload_length) {
  po_status_t status = po35_builder_append_packet(builder, tag_code, payload, payload_length, NULL);
  assert(status == PO_OK);
}

static void manifest_append_settings(po35_builder_t *builder, const uint8_t *chain,
                                     size_t chain_count) {
  po35_settings_t settings = {0};
  uint8_t payload[PO_MAX_PAYLOAD];
  size_t payload_length = 0u;
  assert(chain_count <= PO35_CHAIN_MAX_COUNT);
  settings.chain_count = (uint8_t)chain_count;
  memcpy(settings.chain, chain, chain_count);
  assert(po35_settings_encode(&settings, payload, sizeof(payload), &payload_length) == PO_OK);
  manifest_append(builder, PO35_TAG_SETTINGS, payload, payload_length);
}

static void manifest_append_pattern(po35_builder_t *builder, uint8_t index, uint8_t speech_event) {
  po35_pattern_t pattern = {0};
  uint8_t payload[PO35_PATTERN_BYTES];
  pattern.pattern_index = index;
  pattern.speech_events[0] = speech_event;
  assert(po35_pattern_encode(&pattern, payload, sizeof(payload)) == PO_OK);
  manifest_append(builder, PO35_TAG_PATTERN, payload, sizeof(payload));
}

static void manifest_append_empty_pattern(po35_builder_t *builder, uint8_t index) {
  manifest_append(builder, PO35_TAG_PATTERN_EMPTY, &index, 1u);
}

static void manifest_append_speech_header(po35_builder_t *builder, uint8_t slot) {
  po35_speech_header_t header = {.slot = slot};
  uint8_t payload[PO35_SPEECH_HEADER_BYTES];
  assert(po35_speech_header_encode(&header, payload, sizeof(payload)) == PO_OK);
  manifest_append(builder, PO35_TAG_SPEECH_HEADER, payload, sizeof(payload));
  manifest_append(builder, PO35_TAG_SPEECH_END, NULL, 0u);
}

static void manifest_read_text(const char *path, char *text, size_t capacity) {
  uint8_t *bytes = NULL;
  size_t length = 0u;
  assert(po_frame_read_file(path, &bytes, &length));
  assert(length < capacity);
  memcpy(text, bytes, length);
  text[length] = '\0';
  free(bytes);
}

static int manifest_run_cli(int argc, char **argv, char *stdout_text, size_t stdout_capacity,
                            char *stderr_text, size_t stderr_capacity) {
  FILE *stdout_file = fopen("manifest-stdout.txt", "wb");
  FILE *stderr_file = fopen("manifest-stderr.txt", "wb");
  int stdout_fd, stderr_fd, status, rc;

  assert(stdout_file != NULL && stderr_file != NULL);
  status = fflush(stdout);
  assert(status == 0);
  status = fflush(stderr);
  assert(status == 0);
  stdout_fd = dup(STDOUT_FILENO);
  stderr_fd = dup(STDERR_FILENO);
  assert(stdout_fd >= 0 && stderr_fd >= 0);
  status = dup2(fileno(stdout_file), STDOUT_FILENO);
  assert(status == STDOUT_FILENO);
  status = dup2(fileno(stderr_file), STDERR_FILENO);
  assert(status == STDERR_FILENO);
  rc = po35_transfer_manifest_main(argc, argv);
  status = fflush(stdout);
  assert(status == 0);
  status = fflush(stderr);
  assert(status == 0);
  status = dup2(stdout_fd, STDOUT_FILENO);
  assert(status == STDOUT_FILENO);
  status = dup2(stderr_fd, STDERR_FILENO);
  assert(status == STDERR_FILENO);
  status = close(stdout_fd);
  assert(status == 0);
  status = close(stderr_fd);
  assert(status == 0);
  status = fclose(stdout_file);
  assert(status == 0);
  status = fclose(stderr_file);
  assert(status == 0);
  manifest_read_text("manifest-stdout.txt", stdout_text, stdout_capacity);
  manifest_read_text("manifest-stderr.txt", stderr_text, stderr_capacity);
  return rc;
}

static size_t manifest_matching_frame(uint8_t *frame, size_t capacity, uint8_t header_slot) {
  const uint8_t chain[] = {2u, 5u};
  po35_builder_t builder;
  size_t length = 0u;
  assert(po35_builder_init(&builder, frame, capacity) == PO_OK);
  manifest_append_settings(&builder, chain, sizeof(chain));
  manifest_append_pattern(&builder, 2u, 0x31u);
  manifest_append_empty_pattern(&builder, 5u);
  manifest_append_speech_header(&builder, header_slot);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  return length;
}

static void test_manifest(void) {
  uint8_t frame[8192];
  char stdout_text[2048], stderr_text[1024];
  char *argv[] = {"po35_transfer_manifest", "frame.bin", NULL};
  po35_builder_t builder;
  size_t length;

  length = manifest_matching_frame(frame, sizeof(frame), 3u);
  manifest_save("frame.bin", frame, length);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 0);
  assert(stderr_text[0] == '\0' &&
         strstr(stdout_text, "pattern_records=2 full_patterns=1 empty_patterns=1") != NULL &&
         strstr(stdout_text, "chain_pattern_set=match") != NULL &&
         strstr(stdout_text, "speech_header_event_set=match") != NULL &&
         strstr(stdout_text, "shape=observed-partial-candidate") != NULL &&
         strstr(stdout_text, "projection=none") != NULL);

  length = manifest_matching_frame(frame, sizeof(frame), 4u);
  manifest_save("frame.bin", frame, length);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 0);
  assert(strstr(stdout_text, "speech_header_event_set=mismatch") != NULL &&
         strstr(stdout_text, "shape=unclassified") != NULL);

  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  {
    const uint8_t chain[] = {2u};
    manifest_append_settings(&builder, chain, sizeof(chain));
  }
  manifest_append_empty_pattern(&builder, 2u);
  manifest_append_empty_pattern(&builder, 2u);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  manifest_save("frame.bin", frame, length);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 0);
  assert(strstr(stdout_text, "chain_pattern_set=not-evaluable") != NULL &&
         strstr(stdout_text, "shape=unclassified") != NULL);

  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  {
    const uint8_t chain[] = {2u};
    manifest_append_settings(&builder, chain, sizeof(chain));
    manifest_append_settings(&builder, chain, sizeof(chain));
  }
  manifest_append_empty_pattern(&builder, 2u);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  manifest_save("frame.bin", frame, length);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 0);
  assert(strstr(stdout_text, "chain_pattern_set=not-evaluable") != NULL &&
         strstr(stdout_text, "shape=unclassified") != NULL);

  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  {
    const uint8_t chain[] = {2u};
    po35_speech_header_t header = {.slot = 3u};
    uint8_t payload[PO35_SPEECH_HEADER_BYTES];
    manifest_append_settings(&builder, chain, sizeof(chain));
    manifest_append_pattern(&builder, 2u, 0x31u);
    assert(po35_speech_header_encode(&header, payload, sizeof(payload)) == PO_OK);
    manifest_append(&builder, PO35_TAG_SPEECH_HEADER, payload, sizeof(payload));
  }
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  manifest_save("frame.bin", frame, length);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 0);
  assert(strstr(stdout_text, "chain_pattern_set=match") != NULL &&
         strstr(stdout_text, "speech_header_event_set=not-evaluable") != NULL &&
         strstr(stdout_text, "shape=unclassified") != NULL);

  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  {
    const uint8_t chain[] = {2u};
    po35_speech_header_t header = {.slot = 3u};
    uint8_t payload[PO35_SPEECH_HEADER_BYTES];
    manifest_append_settings(&builder, chain, sizeof(chain));
    manifest_append_pattern(&builder, 2u, 0x31u);
    assert(po35_speech_header_encode(&header, payload, sizeof(payload)) == PO_OK);
    manifest_append(&builder, PO35_TAG_SPEECH_HEADER, payload, sizeof(payload));
    manifest_append(&builder, 0xdead, NULL, 0u);
    manifest_append(&builder, PO35_TAG_SPEECH_END, NULL, 0u);
  }
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  manifest_save("frame.bin", frame, length);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 0);
  assert(strstr(stdout_text, "chain_pattern_set=match") != NULL &&
         strstr(stdout_text, "speech_header_event_set=not-evaluable") != NULL &&
         strstr(stdout_text, "shape=unclassified") != NULL);

  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  manifest_append(&builder, PO35_TAG_PATTERN, (const uint8_t *)"x", 1u);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  manifest_save("frame.bin", frame, length);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 0);
  assert(strstr(stdout_text, "pattern_records=1 full_patterns=1 empty_patterns=0") != NULL &&
         strstr(stdout_text, "chain_pattern_set=not-evaluable") != NULL &&
         strstr(stdout_text, "shape=unclassified") != NULL);

  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  {
    const uint8_t chain[] = {16u};
    manifest_append_settings(&builder, chain, sizeof(chain));
  }
  manifest_append_empty_pattern(&builder, 16u);
  manifest_append_speech_header(&builder, PO35_SPEECH_SLOT_COUNT);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  manifest_save("frame.bin", frame, length);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 0);
  assert(strstr(stdout_text, "chain_pattern_set=not-evaluable") != NULL &&
         strstr(stdout_text, "speech_header_event_set=not-evaluable") != NULL &&
         strstr(stdout_text, "shape=unclassified") != NULL);

  assert(po35_builder_init(&builder, frame, sizeof(frame)) == PO_OK);
  for (uint8_t index = 0u; index < 16u; ++index)
    manifest_append_empty_pattern(&builder, index);
  assert(po35_builder_finish(&builder, &length) == PO_OK);
  manifest_save("frame.bin", frame, length);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 0);
  assert(strstr(stdout_text, "shape=full-bank-like") != NULL &&
         strstr(stdout_text, "projection=none") != NULL);

  frame[length] = 0xa5u;
  manifest_save("frame.bin", frame, length + 1u);
  assert(manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                          sizeof(stderr_text)) == 1);
  assert(stdout_text[0] == '\0' && strstr(stderr_text, "not one complete PO-35 frame") != NULL);
}

int main(void) {
  char directory[128];
  int made = 0;
  int status;
  for (unsigned int index = 0u; index < 100u && !made; ++index) {
    snprintf(directory, sizeof(directory), "/tmp/libpo35-manifest-%ld-%u", (long)getpid(), index);
    made = mkdir(directory, 0700) == 0;
    assert(made || errno == EEXIST);
  }
  assert(made);
  status = chdir(directory);
  assert(status == 0);
  test_manifest();
  puts("PO-35 transfer manifest tests passed");
  return 0;
}
