cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PO32_VERSION LIMIT_COUNT 1)
project(libpo32 VERSION ${PO32_VERSION} LANGUAGES C)

enable_testing()
add_subdirectory(core)
