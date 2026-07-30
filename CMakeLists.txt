cmake_minimum_required(VERSION 3.21)

project(HermesLazyCompilationRepro LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT HERMES_SOURCE_DIR)
  message(FATAL_ERROR "Pass -DHERMES_SOURCE_DIR=/absolute/path/to/hermes")
endif()

set(HERMES_ENABLE_DEBUGGER ON CACHE BOOL "" FORCE)
set(HERMES_BUILD_APPLE_FRAMEWORK OFF CACHE BOOL "" FORCE)
set(HERMES_ENABLE_INTL OFF CACHE BOOL "" FORCE)

add_subdirectory("${HERMES_SOURCE_DIR}" hermes)

add_hermes_tool(hermes-lazy-bench
  hermes-lazy-bench.cpp
  LINK_LIBS hermesvm_a
)

set_target_properties(hermes-lazy-bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
