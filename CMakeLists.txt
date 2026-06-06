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
  src/lifter/lifter.cpp
  src/vars/vars.cpp
  src/opt/opt.cpp
  src/emit/emit.cpp
)
target_include_directories(mcore_decompiler PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mcore-proc")  # M*CORE ins.hpp / mcore.hpp
ida_configure_plugin(mcore_decompiler)

# ---------------------------------------------------------------------------
# Reproducible Linux build of the vendored M*CORE processor module (disassembler).
# The install already ships a working procs/mcore.so, so this is OFF by default;
# enable with -DBUILD_MCORE_PROC=ON. Built as a separate .so; not auto-installed.
# ---------------------------------------------------------------------------
option(BUILD_MCORE_PROC "Build the vendored M*CORE processor module" OFF)
set(MCORE_PROC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mcore-proc")
if(BUILD_MCORE_PROC AND EXISTS "${MCORE_PROC_DIR}/reg.cpp")
  add_library(mcore_proc MODULE
    ${MCORE_PROC_DIR}/ana.cpp
    ${MCORE_PROC_DIR}/emu.cpp
    ${MCORE_PROC_DIR}/ins.cpp
    ${MCORE_PROC_DIR}/out.cpp
    ${MCORE_PROC_DIR}/reg.cpp
  )
  target_include_directories(mcore_proc PRIVATE "${MCORE_PROC_DIR}")
  set_target_properties(mcore_proc PROPERTIES CXX_STANDARD 20)
  ida_configure_processor_module(mcore_proc)
endif()

# ---------------------------------------------------------------------------
# Offline unit tests (no IDA dependency).
# ---------------------------------------------------------------------------
enable_testing()
add_executable(unit_tests
  tests/unit/test_pipeline.cpp
  src/vars/vars.cpp
  src/opt/opt.cpp
  src/emit/emit.cpp
)
target_include_directories(unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit")
add_test(NAME unit_tests COMMAND unit_tests)
