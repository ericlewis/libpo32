cmake_minimum_required(VERSION 3.16)

# Offline frame -> transfer WAV -> frame verification for the PO-33/133 and
# PO-35/137 CLIs. WORK_DIR must be new; failed-run artifacts are retained.
foreach(required IN ITEMS ENCODER DECODER INPUT_FRAME WORK_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
  get_filename_component(${required} "${${required}}" ABSOLUTE)
endforeach()
if(NOT DEFINED RATE)
  set(RATE 48000)
endif()
if(NOT "${RATE}" MATCHES "^(44100|48000|96000)$")
  message(FATAL_ERROR "RATE must be 44100, 48000, or 96000")
endif()
foreach(input IN ITEMS ENCODER DECODER INPUT_FRAME)
  if(NOT EXISTS "${${input}}" OR IS_DIRECTORY "${${input}}")
    message(FATAL_ERROR "${input} must name an existing file")
  endif()
endforeach()
if(EXISTS "${WORK_DIR}" OR IS_SYMLINK "${WORK_DIR}")
  message(FATAL_ERROR "WORK_DIR must not already exist")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(transfer_wav "${WORK_DIR}/transfer.wav")
set(decoded_dir "${WORK_DIR}/decoded")
execute_process(
  COMMAND "${ENCODER}" "${INPUT_FRAME}" "${transfer_wav}" --rate "${RATE}"
  RESULT_VARIABLE encode_result
)
if(NOT "${encode_result}" STREQUAL "0")
  message(FATAL_ERROR "Capture encoder failed: ${encode_result}")
endif()
execute_process(
  COMMAND "${DECODER}" "${transfer_wav}" "${decoded_dir}"
  RESULT_VARIABLE decode_result
)
if(NOT "${decode_result}" STREQUAL "0")
  message(FATAL_ERROR "Capture decoder failed: ${decode_result}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${INPUT_FRAME}" "${decoded_dir}/frame.bin"
  RESULT_VARIABLE compare_result
)
if(NOT "${compare_result}" STREQUAL "0")
  message(FATAL_ERROR "Generated waveform did not decode to the exact original frame")
endif()
file(SIZE "${INPUT_FRAME}" frame_bytes)
message(STATUS "Verified ${frame_bytes} identical frame bytes at ${RATE} Hz")
