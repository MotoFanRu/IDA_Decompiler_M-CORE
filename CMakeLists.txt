cmake_minimum_required(VERSION 3.20)
project(mcore_decompiler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(IdaSdk)

# ---------------------------------------------------------------------------
# The IDA plugin (.so). Hex-Rays is reached through the kernel dispatcher
# (get_hexdsp()), so only libida.so is linked.
# ---------------------------------------------------------------------------
add_library(mcore_decompiler SHARED
  src/plugin/plugin.cpp
  src/decoder/decoder.cpp
)
target_include_directories(mcore_decompiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
ida_configure_plugin(mcore_decompiler)

# ---------------------------------------------------------------------------
# Offline unit tests for the decoder (no IDA dependency).
# ---------------------------------------------------------------------------
enable_testing()
add_executable(unit_tests
  tests/unit/test_decoder.cpp
  src/decoder/decoder.cpp
)
target_include_directories(unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit")
add_test(NAME unit_tests COMMAND unit_tests)
