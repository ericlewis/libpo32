if(NOT DEFINED PO32_DEMO)
  message(FATAL_ERROR "PO32_DEMO is required")
endif()

if(NOT DEFINED PO32_DECODE_CAPTURE)
  message(FATAL_ERROR "PO32_DECODE_CAPTURE is required")
endif()

if(NOT DEFINED PO32_SMOKE_DIR)
  message(FATAL_ERROR "PO32_SMOKE_DIR is required")
endif()

set(modem_wav "${PO32_SMOKE_DIR}/demo_modem.wav")
set(output_dir "${PO32_SMOKE_DIR}/decode_capture")
set(summary_path "${output_dir}/summary.txt")
set(pattern_summary_path "${output_dir}/pattern_summary.txt")

file(MAKE_DIRECTORY "${PO32_SMOKE_DIR}")
file(REMOVE_RECURSE "${output_dir}")

execute_process(
  COMMAND "${PO32_DEMO}"
  WORKING_DIRECTORY "${PO32_SMOKE_DIR}"
  RESULT_VARIABLE demo_result
)

if(NOT demo_result EQUAL 0)
  message(FATAL_ERROR "po32_demo smoke test failed: ${demo_result}")
endif()

if(NOT EXISTS "${modem_wav}")
  message(FATAL_ERROR "po32_demo did not write ${modem_wav}")
endif()

execute_process(
  COMMAND "${PO32_DECODE_CAPTURE}" "${modem_wav}" "${output_dir}"
  WORKING_DIRECTORY "${PO32_SMOKE_DIR}"
  RESULT_VARIABLE decode_result
)

if(NOT decode_result EQUAL 0)
  message(FATAL_ERROR "po32_decode_capture smoke test failed: ${decode_result}")
endif()

if(NOT EXISTS "${summary_path}")
  message(FATAL_ERROR "po32_decode_capture did not write ${summary_path}")
endif()

if(NOT EXISTS "${pattern_summary_path}")
  message(FATAL_ERROR "po32_decode_capture did not write ${pattern_summary_path}")
endif()
