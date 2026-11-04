cmake_minimum_required(VERSION 3.16)

# Offline validation of the pinned official PO-33 recording. The decoder
# creates OUTPUT_DIR; an existing file, directory, or symlink is never reused.
foreach(required IN ITEMS DECODER INPUT_WAV OUTPUT_DIR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
  get_filename_component(${required} "${${required}}" ABSOLUTE)
endforeach()

foreach(input IN ITEMS DECODER INPUT_WAV)
  if(NOT EXISTS "${${input}}" OR IS_DIRECTORY "${${input}}")
    message(FATAL_ERROR "${input} must name an existing file")
  endif()
endforeach()
if(EXISTS "${OUTPUT_DIR}" OR IS_SYMLINK "${OUTPUT_DIR}")
  message(FATAL_ERROR "OUTPUT_DIR must not already exist")
endif()

file(SHA256 "${INPUT_WAV}" input_sha256)
if(NOT input_sha256 STREQUAL "6847d14da8a7c7438e84d0f16278988e1db7666c4d8f9942c1ad562efab166e5")
  message(FATAL_ERROR "INPUT_WAV SHA-256 differs from the pinned official recording")
endif()

execute_process(
  COMMAND "${DECODER}" "${INPUT_WAV}" "${OUTPUT_DIR}"
  RESULT_VARIABLE decode_result
)
if(NOT "${decode_result}" STREQUAL "0")
  message(FATAL_ERROR "PO-33 decoder failed: ${decode_result}")
endif()

file(SHA256 "${OUTPUT_DIR}/frame.bin" frame_sha256)
if(NOT frame_sha256 STREQUAL "1580c178c32b2f0d57bccd6f0a912ad0bf02dc7a653fcc5b6ffd3e82e1fe3f50")
  message(FATAL_ERROR "Decoded frame SHA-256 differs from the pinned result")
endif()
file(SHA256 "${OUTPUT_DIR}/payloads.bin" payload_sha256)
if(NOT payload_sha256 STREQUAL "9c5623b9bb4975d4fb4c1bf171e8f21805155c248e4aeb8f32340d3ba2984e52")
  message(FATAL_ERROR "Decoded payload SHA-256 differs from the pinned result")
endif()

file(STRINGS "${OUTPUT_DIR}/summary.txt" summary)
foreach(expected IN ITEMS "packets=4453" "complete=1" "payload_bytes=1005534" "frame_bytes=1031895")
  string(REGEX REPLACE "=.*" "" key "${expected}")
  set(actual "${summary}")
  list(FILTER actual INCLUDE REGEX "^${key}=")
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "Decoded summary must contain exactly ${expected}")
  endif()
endforeach()
file(GLOB pattern_files LIST_DIRECTORIES FALSE "${OUTPUT_DIR}/pattern-*.bin")
file(GLOB sound_files LIST_DIRECTORIES FALSE "${OUTPUT_DIR}/sound-*.wav")
list(LENGTH pattern_files pattern_count)
list(LENGTH sound_files sound_count)
if(NOT pattern_count EQUAL 16 OR NOT sound_count EQUAL 16)
  message(FATAL_ERROR "Expected 16 pattern records and 16 populated sound pools")
endif()
message(STATUS "Verified official PO-33 capture, output hashes, 4453 packets, and 16 patterns/sound pools")
