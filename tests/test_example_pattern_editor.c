#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define main po32_pattern_editor_example_main
#include "../examples/po32_pattern_editor.c"
#undef main

static void test_editor_helpers(void) {
  po32_pattern_editor_t editor;
  int ok;

  editor_init(&editor);
  assert(editor.bank == 0u);
  assert(editor.tempo == 120u);
  assert(editor.swing_times_12 == 0u);
  assert(editor.pattern.pattern_number == 0u);

  assert(instrument_in_selected_bank(&editor, 1));
  assert(!instrument_in_selected_bank(&editor, 9));

  editor.bank = 1u;
  assert(instrument_in_selected_bank(&editor, 9));
  assert(!instrument_in_selected_bank(&editor, 1));

  {
    int value = 0;
    ok = parse_int_arg("12", 0, 20, &value);
    assert(ok);
    assert(value == 12);
    ok = parse_int_arg("abc", 0, 20, &value);
    assert(!ok);
    ok = parse_int_arg("99", 0, 20, &value);
    assert(!ok);
  }

  assert(find_starter_pattern("four-floor") != NULL);
  assert(find_starter_pattern("missing") == NULL);
  assert(find_starter_pattern(NULL) == NULL);
  assert(starter_instrument_for_bank(&editor, 0u) == 0u);

  editor.bank = 0u;
  assert(!apply_starter_pattern(NULL, find_starter_pattern("four-floor")));
  assert(!apply_starter_pattern(&editor, NULL));
  assert(apply_starter_pattern(&editor, find_starter_pattern("four-floor")));
  assert(step_has_accent(&editor, 0));
  assert(editor.pattern.steps[lane_index(0, 0)].instrument == 1u);
  assert(editor.pattern.steps[lane_index(4, 1)].instrument == 2u);
  assert(editor.pattern.steps[lane_index(0, 2)].instrument == 3u);

  clear_step(&editor, 0);
  assert(editor.pattern.steps[lane_index(0, 0)].instrument == 0u);

  assert(add_trigger(&editor, 0, 1));
  assert(editor.pattern.steps[lane_index(0, 0)].instrument == 1u);

  set_step_accent(&editor, 0, 0);
  assert(!step_has_accent(&editor, 0));

  clear_pattern(&editor);
  assert(editor.pattern.pattern_number == 0u);
  assert(editor.pattern.steps[lane_index(0, 0)].instrument == 0u);
}

static void test_editor_io_helpers(void) {
  po32_pattern_editor_t editor;
  uint8_t frame[PO32_EDITOR_FRAME_CAPACITY];
  size_t frame_len = 0u;
  float samples[32] = {0.0f};
  po32_status_t status;

  editor_init(&editor);
  assert(apply_starter_pattern(&editor, find_starter_pattern("four-floor")));

  print_help();
  print_presets();
  print_editor(&editor);

  status = build_pattern_transfer(NULL, frame, sizeof(frame), &frame_len);
  assert(status == PO32_ERR_INVALID_ARG);
  status = build_pattern_transfer(&editor, NULL, sizeof(frame), &frame_len);
  assert(status == PO32_ERR_INVALID_ARG);
  status = build_pattern_transfer(&editor, frame, sizeof(frame), NULL);
  assert(status == PO32_ERR_INVALID_ARG);
  status = build_pattern_transfer(&editor, frame, sizeof(frame), &frame_len);
  assert(status == PO32_OK);
  assert(frame_len > 0u);

  assert(write_binary_file("pattern_editor_good.bin", frame, frame_len));
  assert(!write_binary_file("missing-dir/pattern_editor_bad.bin", frame, frame_len));

  assert(write_wav_file("pattern_editor_good.wav", samples, 0u, 44100u));
  assert(!write_wav_file("missing-dir/pattern_editor_bad.wav", samples, 0u, 44100u));

  assert(export_frame(&editor, "pattern_editor_export.frame"));
  assert(!export_frame(&editor, "missing-dir/pattern_editor_export.frame"));
  assert(export_wav(&editor, "pattern_editor_export.wav", 8000u));
  assert(!export_wav(&editor, "missing-dir/pattern_editor_export.wav", 8000u));
}

static void test_editor_repl_script(void) {
  po32_pattern_editor_t editor;
  const char *script_path = "pattern_editor_repl.txt";
  const char *frame_path = "pattern_editor_repl.frame";
  const char *wav_path = "pattern_editor_repl.wav";
  FILE *script = fopen(script_path, "w");
  FILE *script_input;
  FILE *saved_stdin;

  assert(script != NULL);
  fputs("help\n", script);
  fputs("show\n", script);
  fputs("presets\n", script);
  fputs("preset four-floor\n", script);
  fputs("preset missing\n", script);
  fputs("add 2 9\n", script);
  fputs("add 2 1\n", script);
  fputs("set 2 2 1\n", script);
  fputs("set 2 1 0\n", script);
  fputs("clear 1 1\n", script);
  fputs("clear 2\n", script);
  fputs("accent 1 on\n", script);
  fputs("accent 1 off\n", script);
  fputs("accent 1 toggle\n", script);
  fputs("bank 1\n", script);
  fputs("preset electro\n", script);
  fputs("pattern 3\n", script);
  fputs("tempo 140\n", script);
  fputs("swing 7\n", script);
  fprintf(script, "export-frame %s\n", frame_path);
  fputs("export-wav missing.wav nope\n", script);
  fprintf(script, "export-wav %s 8000\n", wav_path);
  fputs("unknown-command\n", script);
  fputs("quit\n", script);
  fclose(script);

  editor_init(&editor);
  saved_stdin = stdin;
  script_input = fopen(script_path, "r");
  assert(script_input != NULL);
  stdin = script_input;
  repl(&editor);
  stdin = saved_stdin;
  fclose(script_input);

  assert(editor.bank == 1u);
  assert(editor.pattern.pattern_number == 3u);
  assert(editor.tempo == 140u);
  assert(editor.swing_times_12 == 7u);
  assert(editor.pattern.steps[lane_index(0, 0)].instrument == 9u);
  assert(editor.pattern.steps[lane_index(4, 1)].instrument == 10u);

  {
    FILE *frame = fopen(frame_path, "rb");
    FILE *wav = fopen(wav_path, "rb");
    assert(frame != NULL);
    assert(wav != NULL);
    fclose(frame);
    fclose(wav);
  }
}

int main(void) {
  test_editor_helpers();
  test_editor_io_helpers();
  test_editor_repl_script();
  return 0;
}
