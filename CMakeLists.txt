cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PO32_VERSION LIMIT_COUNT 1)
project(libpo32 VERSION ${PO32_VERSION} LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

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

# tests

add_executable(po32_core_tests tests/test_core.c)
target_link_libraries(po32_core_tests PRIVATE po32)

add_executable(po32_synth_tests tests/test_synth.c)
target_link_libraries(po32_synth_tests PRIVATE po32)

add_executable(po32_pattern_editor_tests tests/test_example_pattern_editor.c)
target_link_libraries(po32_pattern_editor_tests PRIVATE po32)

add_executable(po32_decode_capture_tests tests/test_example_decode_capture.c)
target_link_libraries(po32_decode_capture_tests PRIVATE po32)

add_test(NAME po32_core_tests COMMAND po32_core_tests)
add_test(NAME po32_synth_tests COMMAND po32_synth_tests)
add_test(NAME po32_pattern_editor_tests COMMAND po32_pattern_editor_tests)
add_test(NAME po32_decode_capture_tests COMMAND po32_decode_capture_tests)
add_test(NAME po32_example_smoke COMMAND po32_example)
add_test(NAME po32_demo_smoke COMMAND po32_demo)
set_tests_properties(po32_demo_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

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
