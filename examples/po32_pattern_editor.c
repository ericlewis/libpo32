#include "po32.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PO32_EDITOR_FRAME_CAPACITY 2048u
#define PO32_STARTER_PATTERN_COUNT 4u

typedef struct {
  po32_pattern_packet_t pattern;
  uint8_t bank;
  uint8_t tempo;
  uint8_t swing_times_12;
} po32_pattern_editor_t;

typedef struct {
  const char *name;
  const char *description;
  uint16_t accent_bits;
  uint8_t relative_instruments[PO32_PATTERN_LANE_COUNT][PO32_PATTERN_STEP_COUNT];
} po32_starter_pattern_t;

static const po32_starter_pattern_t k_starter_patterns[PO32_STARTER_PATTERN_COUNT] = {
    {
        "four-floor",
        "Straight quarter-note pulse with backbeat and eighth-note hats.",
        0x1111u,
        {
            {1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u},
            {3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u},
            {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        },
    },
    {
        "backbeat",
        "Sparse kick, backbeat snare, offbeat hats, and a late accent hit.",
        0x1010u,
        {
            {1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
            {0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u},
            {0u, 0u, 3u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 3u, 0u, 0u, 0u, 3u, 0u},
            {0u, 0u, 0u, 0u, 0u, 0u, 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 4u},
        },
    },
    {
        "stomp",
        "Busier quarter-grid groove with extra floor hits and steady hats.",
        0x1111u,
        {
            {1u, 0u, 0u, 1u, 0u, 0u, 1u, 0u, 1u, 0u, 0u, 1u, 0u, 0u, 1u, 0u},
            {0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u},
            {3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u, 3u, 0u},
            {0u, 0u, 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 4u, 0u, 0u, 0u, 0u},
        },
    },
    {
        "electro",
        "Broken kick pattern with syncopated hats and a full four-lane stack.",
        0x1111u,
        {
            {1u, 0u, 0u, 0u, 0u, 0u, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 1u, 0u},
            {0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u},
            {0u, 0u, 3u, 0u, 0u, 3u, 0u, 0u, 0u, 0u, 3u, 0u, 0u, 3u, 0u, 0u},
            {0u, 0u, 0u, 4u, 0u, 0u, 0u, 4u, 0u, 0u, 0u, 4u, 0u, 0u, 0u, 4u},
        },
    },
};

static int step_has_accent(const po32_pattern_editor_t *editor, int step_index);

static size_t lane_index(int step_index, int slot_index) {
  return (size_t)slot_index * (size_t)PO32_PATTERN_STEP_COUNT + (size_t)step_index;
}

static int set_trigger_slot(po32_pattern_editor_t *editor, int step_index, int slot_index,
                            uint8_t instrument) {
  if (instrument == 0u) {
    return po32_pattern_clear_trigger(&editor->pattern, (uint8_t)step_index,
                                      (uint8_t)slot_index) == PO32_OK;
  }
  return po32_pattern_set_trigger(&editor->pattern, (uint8_t)step_index, instrument, 1u) ==
         PO32_OK;
}

static void editor_init(po32_pattern_editor_t *editor) {
  memset(editor, 0, sizeof(*editor));
  po32_pattern_init(&editor->pattern, 0u);
  editor->bank = 0u;
  editor->tempo = 120u;
  editor->swing_times_12 = 0u;
}

static int instrument_in_selected_bank(const po32_pattern_editor_t *editor, int instrument) {
  if (editor->bank == 0u) {
    return instrument >= 1 && instrument <= 8;
  }
  return instrument >= 9 && instrument <= 16;
}

static int parse_int_arg(const char *text, int min_value, int max_value, int *out_value) {
  char *end = NULL;
  long value;

  if (text == NULL || out_value == NULL) {
    return 0;
  }

  value = strtol(text, &end, 10);
  if (end == text || *end != '\0' || value < min_value || value > max_value) {
    return 0;
  }

  *out_value = (int)value;
  return 1;
}

static int step_has_accent(const po32_pattern_editor_t *editor, int step_index) {
  return ((editor->pattern.accent_bits >> step_index) & 1u) != 0u;
}

static void set_step_accent(po32_pattern_editor_t *editor, int step_index, int enabled) {
  (void)po32_pattern_set_accent(&editor->pattern, (uint8_t)step_index, enabled);
}

static uint8_t starter_instrument_for_bank(const po32_pattern_editor_t *editor,
                                           uint8_t relative_instrument) {
  if (relative_instrument == 0u) {
    return 0u;
  }
  return (uint8_t)(editor->bank * 8u + relative_instrument);
}

static const po32_starter_pattern_t *find_starter_pattern(const char *name) {
  if (name == NULL) {
    return NULL;
  }
  for (size_t i = 0u; i < PO32_STARTER_PATTERN_COUNT; ++i) {
    if (strcmp(name, k_starter_patterns[i].name) == 0) {
      return &k_starter_patterns[i];
    }
  }
  return NULL;
}

static void print_presets(void) {
  puts("Starter patterns:");
  for (size_t i = 0u; i < PO32_STARTER_PATTERN_COUNT; ++i) {
    printf("  %s - %s\n", k_starter_patterns[i].name, k_starter_patterns[i].description);
  }
}

static int apply_starter_pattern(po32_pattern_editor_t *editor,
                                 const po32_starter_pattern_t *starter) {
  if (editor == NULL || starter == NULL) {
    return 0;
  }

  po32_pattern_clear(&editor->pattern);
  for (int lane = 0; lane < PO32_PATTERN_LANE_COUNT; ++lane) {
    for (int step = 0; step < PO32_PATTERN_STEP_COUNT; ++step) {
      uint8_t relative_instrument = starter->relative_instruments[lane][step];
      if (relative_instrument == 0u) {
        continue;
      }
      if (po32_pattern_set_trigger(&editor->pattern,
                                   (uint8_t)step,
                                   starter_instrument_for_bank(editor, relative_instrument),
                                   1u) != PO32_OK) {
        return 0;
      }
    }
  }
  for (int step = 0; step < PO32_PATTERN_STEP_COUNT; ++step) {
    if (((starter->accent_bits >> step) & 1u) != 0u) {
      set_step_accent(editor, step, 1);
    }
  }
  return 1;
}

static void print_help(void) {
  puts("Commands:");
  puts("  show");
  puts("  presets");
  puts("  preset <name>");
  puts("  add <step> <instrument>");
  puts("  set <step> <lane> <instrument>");
  puts("  clear <step> [lane]");
  puts("  accent <step> [on|off|toggle]");
  puts("  bank <0|1>");
  puts("  pattern <number>");
  puts("  tempo <bpm>");
  puts("  swing <value>");
  puts("  export-frame <path>");
  puts("  export-wav <path> [sample-rate]");
  puts("  help");
  puts("  quit");
  puts("");
  puts("Notes:");
  puts("  - Steps are 1-16.");
  puts("  - Lanes are 1-4. The PO-32 can play one instrument per lane at a time.");
  puts("  - Pattern transfers operate on one bank at a time.");
  puts("      bank 0: sounds 1-8");
  puts("      bank 1: sounds 9-16");
  puts("  - Instrument groups by lane:");
  puts("      lane 1: 1, 5, 9, 13");
  puts("      lane 2: 2, 6, 10, 14");
  puts("      lane 3: 3, 7, 11, 15");
  puts("      lane 4: 4, 8, 12, 16");
  puts("  - 'add' routes by instrument and replaces any existing trigger on that lane.");
  puts("  - Use 0 with 'set' to clear a lane.");
  puts("  - Accent is stored per step, not per slot.");
  puts("  - Starter presets target relative instruments 1-4 in the selected bank.");
  puts("  - Pattern numbers are edited as raw values 0-15 in this example.");
}

static void print_editor(const po32_pattern_editor_t *editor) {
  printf("\npattern=%u bank=%u tempo=%u swing=%u accent_bits=0x%04x\n",
         editor->pattern.pattern_number,
         editor->bank,
         editor->tempo,
         editor->swing_times_12,
         editor->pattern.accent_bits);

  printf("step :");
  for (int step = 0; step < PO32_PATTERN_STEP_COUNT; ++step) {
    printf(" %02d", step + 1);
  }
  putchar('\n');

  for (int slot = 0; slot < PO32_PATTERN_LANE_COUNT; ++slot) {
    printf("lane%d:", slot + 1);
    for (int step = 0; step < PO32_PATTERN_STEP_COUNT; ++step) {
      uint8_t instrument = editor->pattern.steps[lane_index(step, slot)].instrument;
      if (instrument == 0u) {
        printf(" --");
      } else {
        printf(" %02u", instrument);
      }
    }
    putchar('\n');
  }

  printf("accnt:");
  for (int step = 0; step < PO32_PATTERN_STEP_COUNT; ++step) {
    printf(" %s", step_has_accent(editor, step) ? "^^" : "..");
  }
  putchar('\n');
  putchar('\n');
}

static int add_trigger(po32_pattern_editor_t *editor, int step_index, int instrument) {
  return po32_pattern_set_trigger(&editor->pattern, (uint8_t)step_index, (uint8_t)instrument, 1u) ==
         PO32_OK;
}

static int write_binary_file(const char *path, const uint8_t *data, size_t data_len) {
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) {
    return 0;
  }
  if (fwrite(data, 1u, data_len, fp) != data_len) {
    fclose(fp);
    return 0;
  }
  fclose(fp);
  return 1;
}

static int write_wav_file(const char *path, const float *samples, size_t sample_count,
                          uint32_t sample_rate_hz) {
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) {
    return 0;
  }

  {
    uint32_t data_bytes = (uint32_t)(sample_count * 2u);
    uint32_t file_size = 36u + data_bytes;
    uint16_t channels = 1u;
    uint16_t bits = 16u;
    uint32_t byte_rate = sample_rate_hz * channels * (uint32_t)(bits / 8u);
    uint16_t block_align = (uint16_t)(channels * (bits / 8u));
    uint16_t pcm = 1u;
    uint32_t fmt_size = 16u;

    fwrite("RIFF", 1u, 4u, fp);
    fwrite(&file_size, 4u, 1u, fp);
    fwrite("WAVEfmt ", 1u, 8u, fp);
    fwrite(&fmt_size, 4u, 1u, fp);
    fwrite(&pcm, 2u, 1u, fp);
    fwrite(&channels, 2u, 1u, fp);
    fwrite(&sample_rate_hz, 4u, 1u, fp);
    fwrite(&byte_rate, 4u, 1u, fp);
    fwrite(&block_align, 2u, 1u, fp);
    fwrite(&bits, 2u, 1u, fp);
    fwrite("data", 1u, 4u, fp);
    fwrite(&data_bytes, 4u, 1u, fp);
  }

  for (size_t i = 0u; i < sample_count; ++i) {
    float sample = samples[i];
    int16_t pcm_sample;
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;
    pcm_sample = (int16_t)(sample * 32767.0f);
    fwrite(&pcm_sample, 2u, 1u, fp);
  }

  fclose(fp);
  return 1;
}

static po32_status_t build_pattern_transfer(const po32_pattern_editor_t *editor,
                                            uint8_t *frame,
                                            size_t frame_capacity,
                                            size_t *frame_len) {
  po32_builder_t builder;
  po32_state_packet_t state_packet;
  po32_packet_t encoded_packet;
  po32_status_t status;

  if (editor == NULL || frame == NULL || frame_len == NULL) {
    return PO32_ERR_INVALID_ARG;
  }

  po32_builder_init(&builder, frame, frame_capacity);

  status = po32_packet_encode(PO32_TAG_PATTERN, &editor->pattern, &encoded_packet);
  if (status != PO32_OK) {
    return status;
  }
  status = po32_builder_append(&builder, &encoded_packet);
  if (status != PO32_OK) {
    return status;
  }

  memset(&state_packet, 0, sizeof(state_packet));
  po32_morph_pairs_default(state_packet.morph_pairs, PO32_STATE_MORPH_PAIR_COUNT);
  state_packet.tempo = editor->tempo;
  state_packet.swing_times_12 = editor->swing_times_12;
  state_packet.pattern_numbers[0] = editor->pattern.pattern_number;
  state_packet.pattern_count = 1u;

  status = po32_packet_encode(PO32_TAG_STATE, &state_packet, &encoded_packet);
  if (status != PO32_OK) {
    return status;
  }
  status = po32_builder_append(&builder, &encoded_packet);
  if (status != PO32_OK) {
    return status;
  }

  return po32_builder_finish(&builder, frame_len);
}

static int export_frame(const po32_pattern_editor_t *editor, const char *path) {
  uint8_t frame[PO32_EDITOR_FRAME_CAPACITY];
  size_t frame_len = 0u;
  po32_status_t status = build_pattern_transfer(editor, frame, sizeof(frame), &frame_len);

  if (status != PO32_OK) {
    fprintf(stderr, "failed to build frame: %d\n", status);
    return 0;
  }
  if (!write_binary_file(path, frame, frame_len)) {
    fprintf(stderr, "failed to write frame file: %s\n", path);
    return 0;
  }

  printf("wrote %s (%zu bytes)\n", path, frame_len);
  return 1;
}

static int export_wav(const po32_pattern_editor_t *editor, const char *path, uint32_t sample_rate) {
  uint8_t frame[PO32_EDITOR_FRAME_CAPACITY];
  size_t frame_len = 0u;
  size_t sample_count;
  float *samples;
  po32_status_t status = build_pattern_transfer(editor, frame, sizeof(frame), &frame_len);

  if (status != PO32_OK) {
    fprintf(stderr, "failed to build frame: %d\n", status);
    return 0;
  }

  sample_count = po32_render_sample_count(frame_len, sample_rate);
  samples = (float *)malloc(sample_count * sizeof(*samples));
  if (samples == NULL) {
    fputs("failed to allocate modem buffer\n", stderr);
    return 0;
  }

  status = po32_render_dpsk_f32(frame, frame_len, sample_rate, samples, sample_count);
  if (status != PO32_OK) {
    fprintf(stderr, "failed to render transfer audio: %d\n", status);
    free(samples);
    return 0;
  }

  if (!write_wav_file(path, samples, sample_count, sample_rate)) {
    fprintf(stderr, "failed to write wav file: %s\n", path);
    free(samples);
    return 0;
  }

  printf("wrote %s (%zu samples @ %u Hz)\n", path, sample_count, sample_rate);
  free(samples);
  return 1;
}

static void clear_step(po32_pattern_editor_t *editor, int step_index) {
  (void)po32_pattern_clear_step(&editor->pattern, (uint8_t)step_index);
}

static void clear_pattern(po32_pattern_editor_t *editor) {
  po32_pattern_clear(&editor->pattern);
}

static void repl(po32_pattern_editor_t *editor) {
  char line[256];

  print_help();
  print_editor(editor);

  while (1) {
    char *command;
    printf("po32-pattern> ");
    fflush(stdout);

    if (fgets(line, sizeof(line), stdin) == NULL) {
      putchar('\n');
      break;
    }

    command = strtok(line, " \t\r\n");
    if (command == NULL) {
      continue;
    }

    if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
      break;
    }

    if (strcmp(command, "help") == 0) {
      print_help();
      continue;
    }

    if (strcmp(command, "show") == 0) {
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "presets") == 0) {
      print_presets();
      continue;
    }

    if (strcmp(command, "preset") == 0) {
      char *name = strtok(NULL, " \t\r\n");
      const po32_starter_pattern_t *starter = find_starter_pattern(name);
      if (starter == NULL) {
        fputs("usage: preset <four-floor|backbeat|stomp|electro>\n", stderr);
        print_presets();
        continue;
      }
      if (!apply_starter_pattern(editor, starter)) {
        fprintf(stderr, "failed to apply preset: %s\n", starter->name);
        continue;
      }
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "add") == 0) {
      int step = 0;
      int instrument = 0;
      if (!parse_int_arg(strtok(NULL, " \t\r\n"), 1, 16, &step) ||
          !parse_int_arg(strtok(NULL, " \t\r\n"), 1, 16, &instrument)) {
        fputs("usage: add <step 1-16> <instrument 1-16>\n", stderr);
        continue;
      }
      if (!instrument_in_selected_bank(editor, instrument)) {
        fprintf(stderr,
                "instrument %d is not in selected bank %u\n",
                instrument,
                editor->bank);
        continue;
      }
      if (!add_trigger(editor, step - 1, instrument)) {
        fprintf(stderr, "failed to add instrument %d on step %d\n", instrument, step);
        continue;
      }
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "set") == 0) {
      int step = 0;
      int slot = 0;
      int instrument = 0;
      if (!parse_int_arg(strtok(NULL, " \t\r\n"), 1, 16, &step) ||
          !parse_int_arg(strtok(NULL, " \t\r\n"), 1, 4, &slot) ||
          !parse_int_arg(strtok(NULL, " \t\r\n"), 0, 16, &instrument)) {
        fputs("usage: set <step 1-16> <lane 1-4> <instrument 0-16>\n", stderr);
        continue;
      }
      if (instrument != 0 && !instrument_in_selected_bank(editor, instrument)) {
        fprintf(stderr,
                "instrument %d is not in selected bank %u\n",
                instrument,
                editor->bank);
        continue;
      }
      if (instrument != 0) {
        uint8_t expected_lane = 0u;
        if (po32_pattern_trigger_lane((uint8_t)instrument, &expected_lane) != PO32_OK ||
            expected_lane != (uint8_t)(slot - 1)) {
          fprintf(stderr,
                  "instrument %d does not belong to lane %d\n",
                  instrument,
                  slot);
          continue;
        }
      }
      set_trigger_slot(editor, step - 1, slot - 1, (uint8_t)instrument);
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "clear") == 0) {
      int step = 0;
      char *slot_text = NULL;
      if (!parse_int_arg(strtok(NULL, " \t\r\n"), 1, 16, &step)) {
        fputs("usage: clear <step 1-16> [lane 1-4]\n", stderr);
        continue;
      }
      slot_text = strtok(NULL, " \t\r\n");
      if (slot_text != NULL) {
        int slot = 0;
        if (!parse_int_arg(slot_text, 1, 4, &slot)) {
          fputs("usage: clear <step 1-16> [lane 1-4]\n", stderr);
          continue;
        }
        set_trigger_slot(editor, step - 1, slot - 1, 0u);
      } else {
        clear_step(editor, step - 1);
      }
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "accent") == 0) {
      int step = 0;
      char *mode = NULL;
      if (!parse_int_arg(strtok(NULL, " \t\r\n"), 1, 16, &step)) {
        fputs("usage: accent <step 1-16> [on|off|toggle]\n", stderr);
        continue;
      }
      mode = strtok(NULL, " \t\r\n");
      if (mode == NULL || strcmp(mode, "toggle") == 0) {
        set_step_accent(editor, step - 1, !step_has_accent(editor, step - 1));
      } else if (strcmp(mode, "on") == 0) {
        set_step_accent(editor, step - 1, 1);
      } else if (strcmp(mode, "off") == 0) {
        set_step_accent(editor, step - 1, 0);
      } else {
        fputs("usage: accent <step 1-16> [on|off|toggle]\n", stderr);
        continue;
      }
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "bank") == 0) {
      int bank = 0;
      if (!parse_int_arg(strtok(NULL, " \t\r\n"), 0, 1, &bank)) {
        fputs("usage: bank <0|1>\n", stderr);
        continue;
      }
      if (editor->bank != (uint8_t)bank) {
        editor->bank = (uint8_t)bank;
        clear_pattern(editor);
      }
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "pattern") == 0) {
      int pattern_number = 0;
      if (!parse_int_arg(strtok(NULL, " \t\r\n"), 0, 15, &pattern_number)) {
        fputs("usage: pattern <number 0-15>\n", stderr);
        continue;
      }
      editor->pattern.pattern_number = (uint8_t)pattern_number;
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "tempo") == 0) {
      int tempo = 0;
      if (!parse_int_arg(strtok(NULL, " \t\r\n"), 60, 240, &tempo)) {
        fputs("usage: tempo <bpm 60-240>\n", stderr);
        continue;
      }
      editor->tempo = (uint8_t)tempo;
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "swing") == 0) {
      int swing = 0;
      if (!parse_int_arg(strtok(NULL, " \t\r\n"), 0, 255, &swing)) {
        fputs("usage: swing <value 0-255>\n", stderr);
        continue;
      }
      editor->swing_times_12 = (uint8_t)swing;
      print_editor(editor);
      continue;
    }

    if (strcmp(command, "export-frame") == 0) {
      char *path = strtok(NULL, " \t\r\n");
      if (path == NULL) {
        fputs("usage: export-frame <path>\n", stderr);
        continue;
      }
      (void)export_frame(editor, path);
      continue;
    }

    if (strcmp(command, "export-wav") == 0) {
      char *path = strtok(NULL, " \t\r\n");
      char *rate_text = strtok(NULL, " \t\r\n");
      int sample_rate = 44100;
      if (path == NULL) {
        fputs("usage: export-wav <path> [sample-rate]\n", stderr);
        continue;
      }
      if (rate_text != NULL && !parse_int_arg(rate_text, 8000, 192000, &sample_rate)) {
        fputs("usage: export-wav <path> [sample-rate]\n", stderr);
        continue;
      }
      (void)export_wav(editor, path, (uint32_t)sample_rate);
      continue;
    }

    fprintf(stderr, "unknown command: %s\n", command);
  }
}

int main(void) {
  po32_pattern_editor_t editor;

  editor_init(&editor);
  repl(&editor);
  return 0;
}
