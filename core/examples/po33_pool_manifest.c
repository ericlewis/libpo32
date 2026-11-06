/* Hosted description of observed PO-33 sample-pool topology. */
#include "po33.h"
#include "po_frame_report.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct po33_manifest_flag {
  unsigned int value;
  const char *name;
} po33_manifest_flag_t;

static int po33_manifest_writef(FILE *stream, const char *format, ...) {
  va_list arguments;
  int written;
  va_start(arguments, format);
  written = vfprintf(stream, format, arguments);
  va_end(arguments);
  return written >= 0;
}

static int po33_manifest_allocate(size_t count, size_t element_bytes, void **out) {
  if (out == NULL || element_bytes == 0u || count > SIZE_MAX / element_bytes)
    return 0;
  *out = NULL;
  if (count == 0u)
    return 1;
  *out = calloc(count, element_bytes);
  return *out != NULL;
}

static int po33_manifest_write_pool_index(FILE *stream, size_t index) {
  return index == SIZE_MAX ? po33_manifest_writef(stream, "none")
                           : po33_manifest_writef(stream, "%zu", index);
}

static int po33_manifest_write_flags(FILE *stream, unsigned int flags) {
  static const po33_manifest_flag_t flag_names[] = {
      {PO33_SOUND_RECORD_FLAG_HEADER_ONLY, "header-only"},
      {PO33_SOUND_RECORD_FLAG_DUPLICATE_SELECTOR, "duplicate-selector"},
      {PO33_SOUND_RECORD_FLAG_UNKNOWN_EXTENSION, "unknown-extension"},
      {PO33_SOUND_RECORD_FLAG_SOURCE_UNRESOLVED, "source-unresolved"},
      {PO33_SOUND_RECORD_FLAG_SOURCE_AMBIGUOUS, "source-ambiguous"},
  };
  unsigned int remaining = flags;
  int first = 1;

  for (size_t index = 0u; index < sizeof(flag_names) / sizeof(flag_names[0]); ++index) {
    const po33_manifest_flag_t *flag = flag_names + index;
    if ((remaining & flag->value) == 0u)
      continue;
    if (!po33_manifest_writef(stream, "%s%s", first ? "" : ",", flag->name))
      return 0;
    remaining &= ~flag->value;
    first = 0;
  }
  if (remaining != 0u)
    return po33_manifest_writef(stream, "%sunknown(0x%x)", first ? "" : ",", remaining);
  return !first || po33_manifest_writef(stream, "-");
}

static int po33_manifest_count_references(const po33_pool_reference_t *references,
                                          size_t reference_count, size_t pool_count,
                                          size_t *pool_reference_counts) {
  for (size_t index = 0u; index < reference_count; ++index) {
    const po33_pool_reference_t *reference = references + index;
    if (reference->pool_index >= pool_count ||
        pool_reference_counts[reference->pool_index] == SIZE_MAX)
      return 0;
    ++pool_reference_counts[reference->pool_index];
  }
  return 1;
}

static int po33_manifest_write(FILE *stream, const po33_sound_record_t *sound_records,
                               size_t sound_record_count, const po33_pool_t *pools,
                               size_t pool_count, const po33_pool_reference_t *references,
                               size_t reference_count, const size_t *pool_reference_counts) {
  if (!po33_manifest_writef(stream, "sound_records=%zu pools=%zu resolved_references=%zu\n",
                            sound_record_count, pool_count, reference_count))
    return 0;
  for (size_t index = 0u; index < sound_record_count; ++index) {
    const po33_sound_record_t *record = sound_records + index;
    size_t owned_pool_references = 0u;
    size_t referenced_pool_references = 0u;
    if (record->owned_pool_index != SIZE_MAX) {
      if (record->owned_pool_index >= pool_count)
        return 0;
      owned_pool_references = pool_reference_counts[record->owned_pool_index];
    }
    if (record->referenced_pool_index != SIZE_MAX) {
      if (record->referenced_pool_index >= pool_count)
        return 0;
      referenced_pool_references = pool_reference_counts[record->referenced_pool_index];
    }
    if (!po33_manifest_writef(stream,
                              "sound_record=%zu selector=%u range_start_raw=0x%08" PRIx32
                              " range_end_raw=0x%08" PRIx32 " "
                              "source_selector=%u owned_pool=",
                              index, (unsigned)record->selector, record->range_start_raw,
                              record->range_end_raw, (unsigned)record->source_selector) ||
        !po33_manifest_write_pool_index(stream, record->owned_pool_index) ||
        !po33_manifest_writef(stream, " referenced_pool=") ||
        !po33_manifest_write_pool_index(stream, record->referenced_pool_index) ||
        !po33_manifest_writef(stream,
                              " owned_pool_references=%zu referenced_pool_references=%zu "
                              "selection_span=%zu:%zu data_span=%zu:%zu flags=",
                              owned_pool_references, referenced_pool_references,
                              record->selection_span.offset, record->selection_span.length,
                              record->data_span.offset, record->data_span.length) ||
        !po33_manifest_write_flags(stream, record->flags) || !po33_manifest_writef(stream, "\n"))
      return 0;
  }
  for (size_t index = 0u; index < pool_count; ++index) {
    const po33_pool_t *pool = pools + index;
    if (pool->owner_record_index >= sound_record_count ||
        pool->data_bytes <= PO33_SOUND_HEADER_BYTES ||
        !po33_manifest_writef(stream,
                              "pool=%zu owner_sound_record=%zu data_bytes=%zu "
                              "sample_allocation_bytes=%zu data_span=%zu:%zu "
                              "reverse_references=%zu\n",
                              index, pool->owner_record_index, pool->data_bytes,
                              pool->data_bytes - PO33_SOUND_HEADER_BYTES, pool->data_span.offset,
                              pool->data_span.length, pool_reference_counts[index]))
      return 0;
  }
  for (size_t index = 0u; index < reference_count; ++index) {
    const po33_pool_reference_t *reference = references + index;
    if (reference->pool_index >= pool_count ||
        reference->sound_record_index >= sound_record_count ||
        !po33_manifest_writef(stream, "reference=%zu pool=%zu sound_record=%zu\n", index,
                              reference->pool_index, reference->sound_record_index))
      return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  po_frame_report_t report = {0};
  void *sound_records_memory = NULL;
  void *pools_memory = NULL;
  void *references_memory = NULL;
  void *pool_reference_counts_memory = NULL;
  po33_sound_record_t *sound_records = NULL;
  po33_pool_t *pools = NULL;
  po33_pool_reference_t *references = NULL;
  size_t *pool_reference_counts = NULL;
  uint8_t *frame = NULL;
  size_t frame_bytes = 0u;
  size_t sound_record_count = 0u;
  size_t pool_count = 0u;
  size_t reference_count = 0u;
  po_status_t status;
  int rc = 1;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s frame.bin\n", argv[0]);
    return 2;
  }
  if (!po_frame_read_file(argv[1], &frame, &frame_bytes)) {
    fprintf(stderr, "Cannot read frame: %s\n", argv[1]);
    goto cleanup;
  }
  if (!po_frame_report_collect(PO_FRAME_FAMILY_PO33, frame, frame_bytes, &report)) {
    fprintf(stderr, "Cannot inspect PO-33 frame.\n");
    goto cleanup;
  }
  if (!po_frame_report_is_exact_frame(&report)) {
    fprintf(stderr, "Input is not one complete PO-33 frame: %s\n", argv[1]);
    goto cleanup;
  }
  status = po33_pool_topology_requirements(frame, frame_bytes, &sound_record_count, &pool_count,
                                           &reference_count);
  if (status != PO_OK) {
    fprintf(stderr, "Cannot measure PO-33 pool topology: %s\n", po_frame_status_name(status));
    goto cleanup;
  }
  if (!po33_manifest_allocate(sound_record_count, sizeof(*sound_records), &sound_records_memory) ||
      !po33_manifest_allocate(pool_count, sizeof(*pools), &pools_memory) ||
      !po33_manifest_allocate(reference_count, sizeof(*references), &references_memory) ||
      !po33_manifest_allocate(pool_count, sizeof(*pool_reference_counts),
                              &pool_reference_counts_memory)) {
    fprintf(stderr, "Cannot allocate PO-33 pool topology.\n");
    goto cleanup;
  }
  sound_records = sound_records_memory;
  pools = pools_memory;
  references = references_memory;
  pool_reference_counts = pool_reference_counts_memory;
  status = po33_pool_topology_build(frame, frame_bytes, sound_records, sound_record_count, pools,
                                    pool_count, references, reference_count);
  if (status != PO_OK) {
    fprintf(stderr, "Cannot build PO-33 pool topology: %s\n", po_frame_status_name(status));
    goto cleanup;
  }
  if (!po33_manifest_count_references(references, reference_count, pool_count,
                                      pool_reference_counts) ||
      !po33_manifest_write(stdout, sound_records, sound_record_count, pools, pool_count, references,
                           reference_count, pool_reference_counts) ||
      fflush(stdout) != 0) {
    fprintf(stderr, "Cannot write PO-33 pool manifest.\n");
    goto cleanup;
  }
  rc = 0;

cleanup:
  free(pool_reference_counts_memory);
  free(references_memory);
  free(pools_memory);
  free(sound_records_memory);
  po_frame_report_destroy(&report);
  free(frame);
  return rc;
}
