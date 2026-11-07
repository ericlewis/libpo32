include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

option(PO32_SANITIZE "Build library, tests, and examples with ASan+UBSan" OFF)
option(PO32_COVERAGE "Build library, tests, and examples with coverage instrumentation" OFF)
option(PO32_BUILD_FUZZERS "Build libFuzzer harnesses (requires Clang)" OFF)
option(PO32_FUZZ_STANDALONE "Build fuzz harnesses as file-driven replay binaries" OFF)

if(PO32_COVERAGE)
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()

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
  src/po33.c
  src/po33_audio.c
  src/po33_data.c
  src/po35.c
  src/po35_analysis.c
  src/po35_synth.c
  src/po_stereo.c
  src/po32_patch_import.c
  src/po32_synth.c
)
add_library(LibPO32::po32 ALIAS po32)
add_library(LibPO::po ALIAS po32)

set_target_properties(po32 PROPERTIES OUTPUT_NAME po32)
# Keep the shipped core archive free of compiler-generated hosted runtime calls.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(po32 PRIVATE -ffreestanding -fno-builtin -fno-stack-protector)
endif()

target_include_directories(po32
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)


# examples

add_executable(po32_example examples/po32_example.c)
target_link_libraries(po32_example PRIVATE po32)

add_executable(po32_demo examples/po32_demo.c)
target_link_libraries(po32_demo PRIVATE po32)

add_executable(po32_pattern_editor examples/po32_pattern_editor.c)
target_link_libraries(po32_pattern_editor PRIVATE po32)

add_executable(po32_decode_capture examples/po32_decode_capture.c)
target_link_libraries(po32_decode_capture PRIVATE po32)

add_library(po_capture STATIC examples/po_capture.c)
target_link_libraries(po_capture PUBLIC po32)
target_include_directories(po_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/examples)

add_executable(po33_decode_capture examples/po33_decode_capture.c)
target_link_libraries(po33_decode_capture PRIVATE po_capture)

add_library(po_stereo_wav STATIC examples/po_stereo_wav.c)
target_link_libraries(po_stereo_wav PUBLIC po32)
target_include_directories(po_stereo_wav PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/examples)

add_library(po_frame_report STATIC examples/po_frame_report.c)
target_link_libraries(po_frame_report PUBLIC po32)
target_include_directories(po_frame_report PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/examples)

add_executable(po_inspect examples/po_inspect.c)
target_link_libraries(po_inspect PRIVATE po_frame_report)

add_executable(po_diff examples/po_diff.c)
target_link_libraries(po_diff PRIVATE po_frame_report)

add_executable(po_capture_doctor examples/po_capture_doctor.c)
target_link_libraries(po_capture_doctor PRIVATE po_capture po_frame_report)

add_executable(po33_pool_manifest examples/po33_pool_manifest.c)
target_link_libraries(po33_pool_manifest PRIVATE po_frame_report)

add_executable(po35_transfer_manifest examples/po35_transfer_manifest.c)
target_link_libraries(po35_transfer_manifest PRIVATE po_frame_report)

add_executable(po_transfer_verify examples/po_transfer_verify.c)
target_link_libraries(po_transfer_verify PRIVATE po_capture po_frame_report)

add_executable(po33_encode_capture examples/po33_encode_capture.c)
target_link_libraries(po33_encode_capture PRIVATE po_capture po_stereo_wav)

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

add_executable(po33_tests tests/test_po33.c)
target_link_libraries(po33_tests PRIVATE po32)
add_test(NAME po33_tests COMMAND po33_tests)

add_executable(po33_encode_tests tests/test_po33_encode.c)
target_link_libraries(po33_encode_tests PRIVATE po32)
add_test(NAME po33_encode_tests COMMAND po33_encode_tests)

add_executable(po33_transmit_tests tests/test_po33_transmit.c)
target_link_libraries(po33_transmit_tests PRIVATE po_stereo_wav po_capture)
add_test(NAME po33_transmit_tests COMMAND po33_transmit_tests)
set_tests_properties(po33_transmit_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po33_edit_tests tests/test_po33_edit.c)
target_link_libraries(po33_edit_tests PRIVATE po_capture po_stereo_wav)
add_test(NAME po33_edit_tests COMMAND po33_edit_tests)
set_tests_properties(po33_edit_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po33_audio_edit_tests tests/test_po33_audio_edit.c)
target_link_libraries(po33_audio_edit_tests PRIVATE po_capture po_stereo_wav)
add_test(NAME po33_audio_edit_tests COMMAND po33_audio_edit_tests)
set_tests_properties(po33_audio_edit_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po33_capture_tests tests/test_po33_capture.c)
target_link_libraries(po33_capture_tests PRIVATE po_capture)
add_test(NAME po33_capture_tests COMMAND po33_capture_tests)
set_tests_properties(po33_capture_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po33_output_tests tests/test_po33_output.c)
target_link_libraries(po33_output_tests PRIVATE po_capture)
add_test(NAME po33_output_tests COMMAND po33_output_tests)
set_tests_properties(po33_output_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po33_host_coverage_tests tests/test_po33_host_coverage.c)
target_link_libraries(po33_host_coverage_tests PRIVATE po_capture po_stereo_wav)
add_test(NAME po33_host_coverage_tests COMMAND po33_host_coverage_tests
  $<TARGET_FILE:po33_encode_capture> $<TARGET_FILE:po33_decode_capture>)
set_tests_properties(po33_host_coverage_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po_capture_io_tests tests/test_capture_io.c)
target_link_libraries(po_capture_io_tests PRIVATE po_capture po_stereo_wav po_frame_report)
add_test(NAME po_capture_io_tests COMMAND po_capture_io_tests)

add_executable(po33_pool_manifest_tests tests/test_po33_pool_manifest.c)
target_link_libraries(po33_pool_manifest_tests PRIVATE po_frame_report)
add_test(NAME po33_pool_manifest_tests COMMAND po33_pool_manifest_tests)
set_tests_properties(po33_pool_manifest_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po33_pool_edit_tests tests/test_po33_pool_edit.c)
target_link_libraries(po33_pool_edit_tests PRIVATE po32)
add_test(NAME po33_pool_edit_tests COMMAND po33_pool_edit_tests)
add_executable(po33_sound_transfer_tests tests/test_po33_sound_transfer.c)
target_link_libraries(po33_sound_transfer_tests PRIVATE po32)
add_test(NAME po33_sound_transfer_tests COMMAND po33_sound_transfer_tests)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_test(NAME po33_plan_type_tests COMMAND ${CMAKE_COMMAND}
    -DCOMPILER=${CMAKE_C_COMPILER}
    -DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/plan-type-checks
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/verify_po33_plan_types.cmake)
endif()

add_executable(po35_transfer_manifest_tests tests/test_po35_transfer_manifest.c)
target_link_libraries(po35_transfer_manifest_tests PRIVATE po_frame_report)
add_test(NAME po35_transfer_manifest_tests COMMAND po35_transfer_manifest_tests)
set_tests_properties(po35_transfer_manifest_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po_transfer_verify_tests tests/test_transfer_verify.c)
target_link_libraries(po_transfer_verify_tests PRIVATE po_capture po_stereo_wav po_frame_report)
add_test(NAME po_transfer_verify_tests COMMAND po_transfer_verify_tests)
set_tests_properties(po_transfer_verify_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

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
target_link_libraries(po32_decode_capture_tests PRIVATE po_frame_report)

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

add_executable(po35_decode_capture examples/po35_decode_capture.c)
target_link_libraries(po35_decode_capture PRIVATE po_capture)
add_executable(po35_encode_capture examples/po35_encode_capture.c)
target_link_libraries(po35_encode_capture PRIVATE po_capture po_stereo_wav)
add_executable(po35_example_tests tests/test_po35_examples.c)
target_link_libraries(po35_example_tests PRIVATE po_capture po_stereo_wav)
add_test(NAME po35_example_tests COMMAND po35_example_tests)
set_tests_properties(po35_example_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po35_encoding_tests tests/test_po35_encoding.c)
target_link_libraries(po35_encoding_tests PRIVATE po_capture po_stereo_wav)
add_test(NAME po35_encoding_tests COMMAND po35_encoding_tests)
set_tests_properties(po35_encoding_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po35_host_coverage_tests tests/test_po35_host_coverage.c)
target_link_libraries(po35_host_coverage_tests PRIVATE po_capture po_stereo_wav)
add_test(NAME po35_host_coverage_tests COMMAND po35_host_coverage_tests
  $<TARGET_FILE:po35_decode_capture> $<TARGET_FILE:po35_encode_capture>)
set_tests_properties(po35_host_coverage_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(po_math_internal_tests tests/test_math_internal.c)
target_link_libraries(po_math_internal_tests PRIVATE po32)
add_test(NAME po_math_internal_tests COMMAND po_math_internal_tests)
add_executable(po35_analysis_internal_tests tests/test_po35_analysis_internal.c)
target_link_libraries(po35_analysis_internal_tests PRIVATE po32)
add_test(NAME po35_analysis_internal_tests COMMAND po35_analysis_internal_tests)
add_executable(po_family_contract_tests tests/test_family_contracts.c)
target_link_libraries(po_family_contract_tests PRIVATE po32)
add_test(NAME po_family_contract_tests COMMAND po_family_contract_tests)
add_executable(po_stereo_internal_tests tests/test_stereo_internal.c)
target_link_libraries(po_stereo_internal_tests PRIVATE po32)
add_test(NAME po_stereo_internal_tests COMMAND po_stereo_internal_tests)

# Family codecs, shared transport, and the freestanding speech renderer.
foreach(test_name IN ITEMS po33_audio po33_data po35 po35_analysis po35_synth po_stereo po_wire)
  add_executable(${test_name}_tests tests/test_${test_name}.c)
  target_link_libraries(${test_name}_tests PRIVATE po32)
  add_test(NAME ${test_name}_tests COMMAND ${test_name}_tests)
endforeach()

if(UNIX)
  if(PO32_SANITIZE OR PO32_COVERAGE OR PO32_BUILD_FUZZERS)
    # Instrumentation intentionally depends on a host runtime; audit sources
    # separately instead of claiming the instrumented archive is freestanding.
    add_test(NAME po_freestanding_source_symbols
      COMMAND sh ${PROJECT_SOURCE_DIR}/scripts/check-freestanding.sh ${CMAKE_C_COMPILER} ${CMAKE_NM})
  else()
    add_test(NAME po_freestanding_symbols
      COMMAND sh ${PROJECT_SOURCE_DIR}/scripts/check-freestanding.sh
        ${CMAKE_C_COMPILER} ${CMAKE_NM} $<TARGET_FILE:po32> ${CMAKE_AR})
  endif()
endif()

if(UNIX AND NOT CMAKE_CROSSCOMPILING AND NOT PO32_SANITIZE AND NOT PO32_COVERAGE
    AND NOT PO32_BUILD_FUZZERS)
  find_package(PkgConfig QUIET)
  add_test(NAME po_package_smoke
    COMMAND ${CMAKE_COMMAND}
      "-DPO_BUILD_DIR=${PROJECT_BINARY_DIR}"
      "-DPO_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DPO_CONFIG=$<CONFIG>"
      "-DPO_PKG_CONFIG=${PKG_CONFIG_EXECUTABLE}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/run_package_smoke.cmake)
  if(PKG_CONFIG_FOUND)
    add_test(NAME po_package_multiarch_smoke
      COMMAND ${CMAKE_COMMAND}
        "-DPO_BUILD_DIR=${PROJECT_BINARY_DIR}"
        "-DPO_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DPO_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DPO_INSTALL_LIBDIR=lib/x86_64-linux-gnu"
        "-DPO_PKG_CONFIG=${PKG_CONFIG_EXECUTABLE}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/run_package_smoke.cmake)
  endif()
endif()

# Only the core archive is freestanding. Hosted examples/tests may use libm.
if(UNIX AND NOT APPLE)
  get_property(host_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(host_target IN LISTS host_targets)
    if(NOT host_target STREQUAL "po32")
      get_target_property(host_type ${host_target} TYPE)
      if(host_type STREQUAL "EXECUTABLE" OR host_type STREQUAL "STATIC_LIBRARY")
        target_link_libraries(${host_target} PRIVATE m)
      endif()
    endif()
  endforeach()
endif()

# install

install(TARGETS po32
  EXPORT LibPO32Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
  include/po.h
  include/po_common.h
  include/po32.h
  include/po35.h
  include/po35_analysis.h
  include/po35_synth.h
  include/po_stereo.h
  include/po33.h
  include/po32_synth.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT LibPO32Targets
  FILE LibPO32Targets.cmake
  NAMESPACE LibPO32::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibPO32
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibPO32Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/LibPO32Config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibPO32
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/LibPO32ConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/LibPO32Config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/LibPO32ConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibPO32
)

# Derive the relocatable prefix for single-level and multiarch library paths.
file(RELATIVE_PATH PO_PKGCONFIG_PREFIX_RELATIVE
  "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/po32.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/po32.pc
  @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/po32.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Canonical umbrella package; existing LibPO32/po32 discovery remains valid.
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LibPOConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/LibPOConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibPO
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/LibPOConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/LibPOConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/LibPOConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LibPO
)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/po.pc.in ${CMAKE_CURRENT_BINARY_DIR}/po.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/po.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
