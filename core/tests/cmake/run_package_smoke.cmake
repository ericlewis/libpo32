# Install, relocate, and consume both package names with the configured C compiler.
foreach(name IN ITEMS PO_BUILD_DIR PO_C_COMPILER)
  if(NOT DEFINED ${name} OR "${${name}}" STREQUAL "")
    message(FATAL_ERROR "${name} is required")
  endif()
endforeach()
if(NOT EXISTS "${PO_BUILD_DIR}/CMakeCache.txt")
  message(FATAL_ERROR "PO_BUILD_DIR must be a configured build tree")
endif()
set(work "${PO_BUILD_DIR}/package-smoke")
if(IS_SYMLINK "${work}")
  message(FATAL_ERROR "Refusing a symlink for the package test workspace")
endif()
if(EXISTS "${work}")
  if(NOT EXISTS "${work}/.libpo-test-workspace")
    message(FATAL_ERROR "Refusing to replace an unowned package test workspace")
  endif()
  file(REMOVE_RECURSE "${work}")
endif()
file(MAKE_DIRECTORY "${work}")
file(WRITE "${work}/.libpo-test-workspace" "libpo package smoke test\n")
function(run_checked)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 60)
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Package smoke command failed (${result}): ${ARGV}\n${output}\n${error}")
  endif()
endfunction()
set(config_args)
if(DEFINED PO_CONFIG AND NOT "${PO_CONFIG}" STREQUAL "")
  list(APPEND config_args --config "${PO_CONFIG}")
endif()
run_checked("${CMAKE_COMMAND}" --install "${PO_BUILD_DIR}" --prefix "${work}/installed" ${config_args})
file(RENAME "${work}/installed" "${work}/relocated")
file(MAKE_DIRECTORY "${work}/consumer")
file(WRITE "${work}/consumer/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.16)
project(libpo_package_consumer LANGUAGES C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(LibPO CONFIG REQUIRED)
find_package(LibPO32 CONFIG REQUIRED)
add_executable(current consumer.c)
target_link_libraries(current PRIVATE LibPO::po)
add_executable(legacy consumer.c)
target_link_libraries(legacy PRIVATE LibPO32::po32)
set_target_properties(current legacy PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
]=])
file(WRITE "${work}/consumer/consumer.c" [=[
#include <po.h>
int main(void) {
  enum po32_status legacy_status = PO32_OK;
  struct po32_packet legacy_packet = {0};
  unsigned char frame[PO35_PREAMBLE_BYTES + 5];
  po35_builder_t builder;
  po35_decode_result_t decoded;
  po35_synth_t synth;
  po35_synth_voice_t voice;
  size_t length;
  size_t analysis_bytes;
  if (legacy_status != PO_OK || legacy_packet.payload_len != 0) return 1;
  if (po35_builder_init(&builder, frame, sizeof(frame)) != PO_OK) return 2;
  if (po35_builder_finish(&builder, &length) != PO_OK) return 3;
  if (po35_frame_parse(frame, length, 0, 0, &decoded) != PO_OK || !decoded.done) return 4;
  if (po35_synth_init(&synth, PO35_SYNTH_BANK_HIGH, 350) != PO_OK) return 5;
  if (po35_synth_voice_init(&voice, &synth, 0, 0, 80) != PO_OK || !po35_synth_voice_done(&voice)) return 6;
  if (po35_analysis_size_for_pcm(48000, 48000, 80, &analysis_bytes) != PO_OK || analysis_bytes != 80 * 44) return 8;
  return po33_sample_decode(0) == 0 ? 0 : 7;
}
]=])
run_checked("${CMAKE_COMMAND}" -S "${work}/consumer" -B "${work}/consumer-build"
  "-DCMAKE_C_COMPILER=${PO_C_COMPILER}" "-DCMAKE_PREFIX_PATH=${work}/relocated"
  -DCMAKE_BUILD_TYPE=Release)
run_checked("${CMAKE_COMMAND}" --build "${work}/consumer-build" --config Release)
run_checked("${work}/consumer-build/bin/current")
run_checked("${work}/consumer-build/bin/legacy")
