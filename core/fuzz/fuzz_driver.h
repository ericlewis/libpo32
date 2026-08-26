/*
 * Shared fuzz-harness scaffolding.
 *
 * Every harness defines LLVMFuzzerTestOneInput(). Under Clang the libFuzzer
 * runtime provides main(). When PO32_FUZZ_STANDALONE is defined the harness
 * gets a file-driven main() instead, so corpus and crash files can be
 * replayed with any compiler:
 *
 *   ./po32_fuzz_frame_parse corpus/frame_parse/crash-abc123
 */

#ifndef PO32_FUZZ_DRIVER_H
#define PO32_FUZZ_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* Field list for iterating po32_patch_params_t without aliasing games,
   mirroring PO32_PATCH_PARAM_FIELDS in core/src/po32.c. */
/* clang-format off */
#define FUZZ_PATCH_PARAM_FIELDS(X) \
  X(OscWave) X(OscFreq) X(OscAtk)  X(OscDcy) \
  X(ModMode) X(ModRate) X(ModAmt)             \
  X(NFilMod) X(NFilFrq) X(NFilQ)              \
  X(NEnvMod) X(NEnvAtk) X(NEnvDcy)            \
  X(Mix)     X(DistAmt) X(EQFreq)  X(EQGain)  \
  X(Level)   X(OscVel)  X(NVel)    X(ModVel)
/* clang-format on */

/* Abort on violated invariants so the fuzzer records the input. */
static void fuzz_check(int condition) {
  if (!condition) {
    abort();
  }
}

#ifdef PO32_FUZZ_STANDALONE

#include <stdio.h>

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    FILE *f = fopen(argv[i], "rb");
    uint8_t *data;
    long file_size;

    if (f == NULL) {
      fprintf(stderr, "cannot open %s\n", argv[i]);
      return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
      fprintf(stderr, "cannot read %s\n", argv[i]);
      fclose(f);
      return 1;
    }
    data = (uint8_t *)malloc(file_size > 0 ? (size_t)file_size : 1u);
    if (data == NULL) {
      fclose(f);
      return 1;
    }
    if (file_size > 0 && fread(data, 1u, (size_t)file_size, f) != (size_t)file_size) {
      fprintf(stderr, "short read on %s\n", argv[i]);
      free(data);
      fclose(f);
      return 1;
    }
    fclose(f);
    LLVMFuzzerTestOneInput(data, (size_t)file_size);
    free(data);
    printf("ok: %s\n", argv[i]);
  }
  if (argc < 2) {
    LLVMFuzzerTestOneInput((const uint8_t *)"", 0u);
    printf("ok: empty input\n");
  }
  return 0;
}

#endif /* PO32_FUZZ_STANDALONE */

#endif /* PO32_FUZZ_DRIVER_H */
