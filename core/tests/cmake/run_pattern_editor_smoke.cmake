if(NOT DEFINED PO32_PATTERN_EDITOR)
  message(FATAL_ERROR "PO32_PATTERN_EDITOR is required")
endif()

if(NOT DEFINED PO32_SMOKE_DIR)
  message(FATAL_ERROR "PO32_SMOKE_DIR is required")
endif()

file(MAKE_DIRECTORY "${PO32_SMOKE_DIR}")

set(input_path "${PO32_SMOKE_DIR}/pattern_editor_input.txt")
set(frame_path "${PO32_SMOKE_DIR}/pattern_editor.frame")
set(wav_path "${PO32_SMOKE_DIR}/pattern_editor.wav")

file(WRITE "${input_path}"
  "preset four-floor\n"
  "export-frame ${frame_path}\n"
  "export-wav ${wav_path} 44100\n"
  "quit\n")

execute_process(
  COMMAND "${PO32_PATTERN_EDITOR}"
  INPUT_FILE "${input_path}"
  WORKING_DIRECTORY "${PO32_SMOKE_DIR}"
  RESULT_VARIABLE result
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "po32_pattern_editor smoke test failed: ${result}")
endif()

if(NOT EXISTS "${frame_path}")
  message(FATAL_ERROR "po32_pattern_editor did not write ${frame_path}")
endif()

if(NOT EXISTS "${wav_path}")
  message(FATAL_ERROR "po32_pattern_editor did not write ${wav_path}")
endif()
