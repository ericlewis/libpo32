#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

  editor.bank = 0u;
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

static void test_editor_repl_script(void) {
  po32_pattern_editor_t editor;
  const char *script_path = "pattern_editor_repl.txt";
  const char *frame_path = "pattern_editor_repl.frame";
  const char *wav_path = "pattern_editor_repl.wav";
  FILE *script = fopen(script_path, "w");
  FILE *script_input;
  int saved_stdin_fd;

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
  script_input = fopen(script_path, "r");
  assert(script_input != NULL);
  saved_stdin_fd = dup(fileno(stdin));
  assert(saved_stdin_fd >= 0);
  assert(dup2(fileno(script_input), fileno(stdin)) >= 0);
  repl(&editor);
  assert(dup2(saved_stdin_fd, fileno(stdin)) >= 0);
  close(saved_stdin_fd);
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
  test_editor_repl_script();
  return 0;
}
