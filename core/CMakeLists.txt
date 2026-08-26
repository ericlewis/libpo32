include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

option(PO32_SANITIZE "Build library, tests, and examples with ASan+UBSan" OFF)
option(PO32_BUILD_FUZZERS "Build libFuzzer harnesses (requires Clang)" OFF)
option(PO32_FUZZ_STANDALONE "Build fuzz harnesses as file-driven replay binaries" OFF)

if(PO32_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
                      -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

if(PO32_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "PO32_BUILD_FUZZERS requires Clang (got ${CMAKE_C_COMPILER_ID})")
  endif()
  # Instrument everything so libFuzzer coverage guides through the library.
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined
                      -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

# po32 library (pure C, no external dependencies)

add_library(po32 STATIC
  src/po32.c
  src/po32_patch_import.c
  src/po32_synth.c
)

set_target_properties(po32 PROPERTIES OUTPUT_NAME po32)

target_include_directories(po32
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(UNIX AND NOT APPLE)
  target_link_libraries(po32 PUBLIC m)
endif()

# examples

add_executable(po32_example examples/po32_example.c)
target_link_libraries(po32_example PRIVATE po32)

add_executable(po32_demo examples/po32_demo.c)
target_link_libraries(po32_demo PRIVATE po32)

add_executable(po32_pattern_editor examples/po32_pattern_editor.c)
target_link_libraries(po32_pattern_editor PRIVATE po32)

add_executable(po32_decode_capture examples/po32_decode_capture.c)
target_link_libraries(po32_decode_capture PRIVATE po32)

# fuzz harnesses

set(PO32_FUZZ_TARGETS
  frame_parse
  packet_decode
  patch_import
  synth_render
  audio_decode
  modem_roundtrip
)

if(PO32_BUILD_FUZZERS OR PO32_FUZZ_STANDALONE)
  foreach(fuzz_target IN LISTS PO32_FUZZ_TARGETS)
    add_executable(po32_fuzz_${fuzz_target} fuzz/fuzz_${fuzz_target}.c)
    target_link_libraries(po32_fuzz_${fuzz_target} PRIVATE po32)
    target_include_directories(po32_fuzz_${fuzz_target} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
    )
    if(PO32_BUILD_FUZZERS)
      target_compile_options(po32_fuzz_${fuzz_target} PRIVATE -fsanitize=fuzzer)
      target_link_options(po32_fuzz_${fuzz_target} PRIVATE -fsanitize=fuzzer)
    else()
      target_compile_definitions(po32_fuzz_${fuzz_target} PRIVATE PO32_FUZZ_STANDALONE)
    endif()
  endforeach()

  add_executable(po32_fuzz_seed_gen fuzz/fuzz_seed_gen.c)
  target_link_libraries(po32_fuzz_seed_gen PRIVATE po32)
endif()

# tests

add_executable(po32_core_tests tests/test_core.c)
target_link_libraries(po32_core_tests PRIVATE po32)

add_executable(po32_core_internal_tests tests/test_core_internal.c)
target_include_directories(po32_core_internal_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(UNIX AND NOT APPLE)
  target_link_libraries(po32_core_internal_tests PRIVATE m)
endif()

add_executable(po32_patch_import_internal_tests tests/test_patch_import_internal.c)
target_include_directories(po32_patch_import_internal_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(UNIX AND NOT APPLE)
  target_link_libraries(po32_patch_import_internal_tests PRIVATE m)
endif()

add_executable(po32_synth_internal_tests tests/test_synth_internal.c)
target_include_directories(po32_synth_internal_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
if(UNIX AND NOT APPLE)
  target_link_libraries(po32_synth_internal_tests PRIVATE m)
endif()

add_executable(po32_synth_tests tests/test_synth.c)
target_link_libraries(po32_synth_tests PRIVATE po32)

add_executable(po32_pattern_editor_tests tests/test_example_pattern_editor.c)
target_link_libraries(po32_pattern_editor_tests PRIVATE po32)

add_executable(po32_decode_capture_tests tests/test_example_decode_capture.c)
target_link_libraries(po32_decode_capture_tests PRIVATE po32)

add_executable(po32_example_cli_tests tests/test_example_roundtrip_cli.c)
target_link_libraries(po32_example_cli_tests PRIVATE po32)

add_test(NAME po32_core_tests COMMAND po32_core_tests)
add_test(NAME po32_core_internal_tests COMMAND po32_core_internal_tests)
add_test(NAME po32_patch_import_internal_tests COMMAND po32_patch_import_internal_tests)
add_test(NAME po32_synth_internal_tests COMMAND po32_synth_internal_tests)
add_test(NAME po32_synth_tests COMMAND po32_synth_tests)
add_test(NAME po32_pattern_editor_tests COMMAND po32_pattern_editor_tests)
add_test(NAME po32_decode_capture_tests COMMAND po32_decode_capture_tests)
add_test(NAME po32_example_cli_tests COMMAND po32_example_cli_tests)
add_test(NAME po32_example_smoke COMMAND po32_example)
add_test(NAME po32_demo_smoke COMMAND po32_demo)
set_tests_properties(po32_demo_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME po32_demo_modem_alloc_fail COMMAND po32_demo)
set_tests_properties(po32_demo_modem_alloc_fail PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  ENVIRONMENT "PO32_DEMO_FAIL_ALLOC=1"
  WILL_FAIL TRUE
)
add_test(NAME po32_demo_synth_alloc_fail COMMAND po32_demo)
set_tests_properties(po32_demo_synth_alloc_fail PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  ENVIRONMENT "PO32_DEMO_FAIL_ALLOC=2"
  WILL_FAIL TRUE
)

add_test(
  NAME po32_pattern_editor_smoke
  COMMAND ${CMAKE_COMMAND}
    -DPO32_PATTERN_EDITOR=$<TARGET_FILE:po32_pattern_editor>
    -DPO32_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke-pattern-editor
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/run_pattern_editor_smoke.cmake
)

add_test(
  NAME po32_decode_capture_smoke
  COMMAND ${CMAKE_COMMAND}
    -DPO32_DEMO=$<TARGET_FILE:po32_demo>
    -DPO32_DECODE_CAPTURE=$<TARGET_FILE:po32_decode_capture>
    -DPO32_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke-decode-capture
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/run_decode_capture_smoke.cmake
)

# install

install(TARGETS po32
  EXPORT LibPO32Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
  include/po32.h
  include/po32_synth.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT LibPO32Targets
  FILE LibPO32Targets.cmake
  NAMESPACE LibPO32::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibPO32
)
