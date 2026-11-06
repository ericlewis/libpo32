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

#include "po33.h"
#include "po_frame_report.h"

#define main po33_pool_manifest_main
#include "../examples/po33_pool_manifest.c"
#undef main

typedef struct pool_manifest_fixture {
  uint8_t frame[8192];
  size_t length;
} pool_manifest_fixture_t;

static void pool_manifest_save(const char *path, const uint8_t *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  size_t written;
  int status;
  assert(file != NULL);
  written = fwrite(bytes, 1u, length, file);
  status = fclose(file);
  assert(written == length && status == 0);
}

static void pool_manifest_read_text(const char *path, char *text, size_t capacity) {
  uint8_t *bytes = NULL;
  size_t length = 0u;
  assert(po_frame_read_file(path, &bytes, &length));
  assert(length < capacity);
  memcpy(text, bytes, length);
  text[length] = '\0';
  free(bytes);
}

static int pool_manifest_run_cli(int argc, char **argv, char *stdout_text, size_t stdout_capacity,
                                 char *stderr_text, size_t stderr_capacity) {
  FILE *stdout_file = fopen("pool-manifest-stdout.txt", "wb");
  FILE *stderr_file = fopen("pool-manifest-stderr.txt", "wb");
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
  rc = po33_pool_manifest_main(argc, argv);
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
  pool_manifest_read_text("pool-manifest-stdout.txt", stdout_text, stdout_capacity);
  pool_manifest_read_text("pool-manifest-stderr.txt", stderr_text, stderr_capacity);
  return rc;
}

static void pool_manifest_append(po33_builder_t *builder, uint16_t tag_code, const uint8_t *payload,
                                 size_t payload_length, size_t *packet_offset) {
  po_status_t status =
      po33_builder_append_packet(builder, tag_code, payload, payload_length, packet_offset);
  assert(status == PO_OK);
}

static void pool_manifest_header(uint8_t out[PO33_SOUND_HEADER_BYTES], uint32_t range_start,
                                 uint32_t range_end, uint8_t source_selector) {
  po33_sound_header_t header = {
      .range_start_raw = range_start,
      .range_end_raw = range_end,
      .source_selector = source_selector,
  };
  assert(po33_sound_header_encode(&header, out, PO33_SOUND_HEADER_BYTES) == PO_OK);
}

static void pool_manifest_append_sound(po33_builder_t *builder, uint8_t selector,
                                       uint32_t range_start, uint32_t range_end,
                                       uint8_t source_selector, const uint8_t *samples,
                                       size_t sample_count) {
  uint8_t data[PO33_SOUND_HEADER_BYTES + 2u];
  assert(sample_count <= 2u);
  pool_manifest_append(builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  pool_manifest_header(data, range_start, range_end, source_selector);
  if (sample_count != 0u)
    memcpy(data + PO33_SOUND_HEADER_BYTES, samples, sample_count);
  pool_manifest_append(builder, PO33_TAG_SOUND_DATA, data, PO33_SOUND_HEADER_BYTES + sample_count,
                       NULL);
}

static void pool_manifest_make_fixture(pool_manifest_fixture_t *fixture) {
  po33_builder_t builder;
  uint8_t selector;
  uint8_t data[PO33_SOUND_HEADER_BYTES + 2u];
  const uint8_t pool_zero[] = {0x91u, 0x92u};
  const uint8_t one_sample[] = {0x93u};
  const uint8_t pool_extension[] = {0x94u, 0x95u};
  const uint8_t unknown[] = {0x5au, 0x51u, 0x50u};

  assert(po33_builder_init(&builder, fixture->frame, sizeof(fixture->frame)) == PO_OK);
  pool_manifest_append_sound(&builder, 7u, UINT32_C(0x10203040), UINT32_C(0x50607080), 7u,
                             pool_zero, sizeof(pool_zero));
  pool_manifest_append_sound(&builder, 8u, 11u, 22u, 7u, NULL, 0u);

  selector = 10u;
  pool_manifest_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  pool_manifest_header(data, 33u, 44u, 10u);
  memcpy(data + PO33_SOUND_HEADER_BYTES, pool_extension, sizeof(pool_extension));
  pool_manifest_append(&builder, PO33_TAG_SOUND_DATA, data, 4u, NULL);
  pool_manifest_append(&builder, 0x4321u, unknown, sizeof(unknown), NULL);
  pool_manifest_append(&builder, PO33_TAG_SOUND_DATA, data + 4u, 7u, NULL);

  pool_manifest_append_sound(&builder, 12u, 55u, 66u, 12u, one_sample, sizeof(one_sample));
  pool_manifest_append_sound(&builder, 12u, 77u, 88u, 12u, NULL, 0u);
  pool_manifest_append_sound(&builder, 15u, 99u, 111u, 15u, one_sample, sizeof(one_sample));
  pool_manifest_append_sound(&builder, 15u, 112u, 123u, 15u, one_sample, sizeof(one_sample));
  pool_manifest_append_sound(&builder, 14u, 124u, 135u, 99u, NULL, 0u);

  selector = 17u;
  pool_manifest_append(&builder, PO33_TAG_SOUND_SELECT, &selector, 1u, NULL);
  assert(po33_builder_finish(&builder, &fixture->length) == PO_OK);
}

static size_t pool_manifest_make_malformed_frame(uint8_t *frame, size_t capacity) {
  po33_builder_t builder;
  uint8_t header[PO33_SOUND_HEADER_BYTES];
  size_t length = 0u;
  assert(po33_builder_init(&builder, frame, capacity) == PO_OK);
  pool_manifest_header(header, 0u, 1u, 0u);
  pool_manifest_append(&builder, PO33_TAG_SOUND_DATA, header, sizeof(header), NULL);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  return length;
}

static size_t pool_manifest_make_empty_frame(uint8_t *frame, size_t capacity) {
  po33_builder_t builder;
  size_t length = 0u;
  assert(po33_builder_init(&builder, frame, capacity) == PO_OK);
  assert(po33_builder_finish(&builder, &length) == PO_OK);
  return length;
}

static void test_pool_manifest(void) {
  pool_manifest_fixture_t fixture;
  uint8_t malformed[8192];
  uint8_t empty[8192];
  po33_sound_record_t sound_records[8];
  po33_pool_t pools[5];
  po33_pool_reference_t references[5];
  char stdout_text[8192], stderr_text[1024], expected_span[512];
  char *argv[] = {"po33_pool_manifest", "frame.bin", NULL};
  size_t malformed_length;
  size_t empty_length;
  size_t sound_record_count = 0u, pool_count = 0u, reference_count = 0u;
  int written;

  pool_manifest_make_fixture(&fixture);
  pool_manifest_save("frame.bin", fixture.frame, fixture.length);
  assert(pool_manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                               sizeof(stderr_text)) == 0);
  assert(po33_pool_topology_requirements(fixture.frame, fixture.length, &sound_record_count,
                                         &pool_count, &reference_count) == PO_OK);
  assert(sound_record_count == 8u && pool_count == 5u && reference_count == 5u);
  assert(po33_pool_topology_build(fixture.frame, fixture.length, sound_records,
                                  sizeof(sound_records) / sizeof(sound_records[0]), pools,
                                  sizeof(pools) / sizeof(pools[0]), references,
                                  sizeof(references) / sizeof(references[0])) == PO_OK);
  assert(stderr_text[0] == '\0' &&
         strstr(stdout_text, "sound_records=8 pools=5 resolved_references=5\n") == stdout_text &&
         strstr(stdout_text,
                "sound_record=0 selector=7 range_start_raw=0x10203040 range_end_raw=0x50607080 "
                "source_selector=7 owned_pool=0 referenced_pool=0 "
                "owned_pool_references=2 referenced_pool_references=2") != NULL &&
         strstr(stdout_text,
                "sound_record=1 selector=8 range_start_raw=0x0000000b range_end_raw=0x00000016 "
                "source_selector=7 owned_pool=none referenced_pool=0 "
                "owned_pool_references=0 referenced_pool_references=2") != NULL &&
         strstr(stdout_text,
                "pool=0 owner_sound_record=0 data_bytes=11 sample_allocation_bytes=2") != NULL &&
         strstr(stdout_text,
                "pool=1 owner_sound_record=2 data_bytes=11 sample_allocation_bytes=2") != NULL &&
         strstr(stdout_text, "reference=0 pool=0 sound_record=0\n") != NULL &&
         strstr(stdout_text, "reference=4 pool=2 sound_record=4\n") != NULL &&
         strstr(stdout_text, "selector=17") == NULL);

  written = snprintf(expected_span, sizeof(expected_span),
                     "sound_record=2 selector=10 range_start_raw=0x00000021 "
                     "range_end_raw=0x0000002c source_selector=10 owned_pool=1 "
                     "referenced_pool=1 owned_pool_references=1 referenced_pool_references=1 "
                     "selection_span=%zu:%zu data_span=%zu:%zu flags=unknown-extension\n",
                     sound_records[2].selection_span.offset, sound_records[2].selection_span.length,
                     sound_records[2].data_span.offset, sound_records[2].data_span.length);
  assert(written > 0 && (size_t)written < sizeof(expected_span));
  assert(strstr(stdout_text, expected_span) != NULL);

  written = snprintf(expected_span, sizeof(expected_span),
                     "sound_record=3 selector=12 range_start_raw=0x00000037 "
                     "range_end_raw=0x00000042 source_selector=12 owned_pool=2 "
                     "referenced_pool=2 owned_pool_references=2 referenced_pool_references=2 "
                     "selection_span=%zu:%zu data_span=%zu:%zu flags=duplicate-selector\n",
                     sound_records[3].selection_span.offset, sound_records[3].selection_span.length,
                     sound_records[3].data_span.offset, sound_records[3].data_span.length);
  assert(written > 0 && (size_t)written < sizeof(expected_span));
  assert(strstr(stdout_text, expected_span) != NULL);

  written =
      snprintf(expected_span, sizeof(expected_span),
               "sound_record=5 selector=15 range_start_raw=0x00000063 range_end_raw=0x0000006f "
               "source_selector=15 owned_pool=3 referenced_pool=none owned_pool_references=0 "
               "referenced_pool_references=0 selection_span=%zu:%zu data_span=%zu:%zu "
               "flags=duplicate-selector,source-ambiguous\n",
               sound_records[5].selection_span.offset, sound_records[5].selection_span.length,
               sound_records[5].data_span.offset, sound_records[5].data_span.length);
  assert(written > 0 && (size_t)written < sizeof(expected_span));
  assert(strstr(stdout_text, expected_span) != NULL);

  written = snprintf(expected_span, sizeof(expected_span),
                     "sound_record=7 selector=14 range_start_raw=0x0000007c "
                     "range_end_raw=0x00000087 source_selector=99 owned_pool=none "
                     "referenced_pool=none owned_pool_references=0 referenced_pool_references=0 "
                     "selection_span=%zu:%zu data_span=%zu:%zu "
                     "flags=header-only,source-unresolved\n",
                     sound_records[7].selection_span.offset, sound_records[7].selection_span.length,
                     sound_records[7].data_span.offset, sound_records[7].data_span.length);
  assert(written > 0 && (size_t)written < sizeof(expected_span));
  assert(strstr(stdout_text, expected_span) != NULL);

  fixture.frame[fixture.length] = 0xa5u;
  pool_manifest_save("frame.bin", fixture.frame, fixture.length + 1u);
  assert(pool_manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                               sizeof(stderr_text)) == 1);
  assert(stdout_text[0] == '\0' &&
         strstr(stderr_text, "Input is not one complete PO-33 frame") != NULL);

  malformed_length = pool_manifest_make_malformed_frame(malformed, sizeof(malformed));
  pool_manifest_save("frame.bin", malformed, malformed_length);
  assert(pool_manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                               sizeof(stderr_text)) == 1);
  assert(stdout_text[0] == '\0' &&
         strstr(stderr_text, "Cannot measure PO-33 pool topology: frame") != NULL);

  empty_length = pool_manifest_make_empty_frame(empty, sizeof(empty));
  pool_manifest_save("frame.bin", empty, empty_length);
  assert(pool_manifest_run_cli(2, argv, stdout_text, sizeof(stdout_text), stderr_text,
                               sizeof(stderr_text)) == 0);
  assert(stderr_text[0] == '\0' &&
         strcmp(stdout_text, "sound_records=0 pools=0 resolved_references=0\n") == 0);
}

int main(void) {
  char directory[128];
  int made = 0;
  int status;
  for (unsigned int index = 0u; index < 100u && !made; ++index) {
    snprintf(directory, sizeof(directory), "/tmp/libpo33-pool-manifest-%ld-%u", (long)getpid(),
             index);
    made = mkdir(directory, 0700) == 0;
    assert(made || errno == EEXIST);
  }
  assert(made);
  status = chdir(directory);
  assert(status == 0);
  test_pool_manifest();
  puts("PO-33 pool manifest tests passed");
  return 0;
}
