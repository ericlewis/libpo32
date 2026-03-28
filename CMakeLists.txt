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

# tests

add_executable(po32_core_tests tests/test_core.c)
target_link_libraries(po32_core_tests PRIVATE po32)

add_executable(po32_synth_tests tests/test_synth.c)
target_link_libraries(po32_synth_tests PRIVATE po32)

add_test(NAME po32_core_tests COMMAND po32_core_tests)
add_test(NAME po32_synth_tests COMMAND po32_synth_tests)

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
