cmake_minimum_required(VERSION 3.25)

# IDA 9.4 publishes its C++ SDK separately from the application install. Use
# the official SDK's out-of-tree CMake entrypoint (tag v9.4.0-release).
if(DEFINED IDASDK)
  set(_idasdk_root "${IDASDK}")
elseif(DEFINED ENV{IDASDK} AND NOT "$ENV{IDASDK}" STREQUAL "")
  set(_idasdk_root "$ENV{IDASDK}")
else()
  message(FATAL_ERROR
    "IDASDK is not set. Point it at the HexRaysSA/ida-sdk v9.4.0-release checkout.")
endif()
if(EXISTS "${_idasdk_root}/src/include/pro.h")
  set(_idasdk_source_root "${_idasdk_root}/src")
elseif(EXISTS "${_idasdk_root}/include/pro.h")
  set(_idasdk_source_root "${_idasdk_root}")
else()
  message(FATAL_ERROR "include/pro.h not found below IDASDK=${_idasdk_root}")
endif()
set(IDASDK "${_idasdk_source_root}" CACHE PATH "IDA SDK source root" FORCE)
set(ENV{IDASDK} "${_idasdk_source_root}")
set(_idasdk_init "${_idasdk_source_root}/cmake/idasdk_init.cmake")
include("${_idasdk_init}")

project(mcore_decompiler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep this out-of-tree plugin's output in its own build directory rather than
# the SDK checkout's bin/plugins directory.
set(IDA_PLUGIN_DIR "${CMAKE_BINARY_DIR}")
find_package(idasdk REQUIRED)
if(NOT IDA_SDK_VERSION_NUM EQUAL 940)
  message(FATAL_ERROR
    "IDA SDK 9.4 is required (found ${idasdk_VERSION} at ${IDASDK})")
endif()

# ---------------------------------------------------------------------------
# The IDA plugin (.so), linked through the official SDK plugin target.
# ---------------------------------------------------------------------------
ida_add_plugin(mcore_decompiler
  SOURCES
    src/plugin/plugin.cpp
    src/lifter/lifter.cpp
    src/vars/vars.cpp
    src/opt/opt.cpp
    src/opt/stackframe.cpp
    src/emit/emit.cpp
)
target_include_directories(mcore_decompiler PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(mcore_decompiler PROPERTIES SKIP_BUILD_RPATH TRUE)

# ---------------------------------------------------------------------------
# Offline unit tests (no IDA dependency).
# ---------------------------------------------------------------------------
enable_testing()
add_executable(unit_tests
  tests/unit/test_pipeline.cpp
  src/vars/vars.cpp
  src/opt/opt.cpp
  src/opt/stackframe.cpp
  src/emit/emit.cpp
)
target_include_directories(unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit")
add_test(NAME unit_tests COMMAND unit_tests)
