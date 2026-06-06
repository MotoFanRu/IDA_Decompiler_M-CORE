# Locate the IDA Pro 9 installation that ships the SDK headers and link libs.
#
# Override with -DIDA_DIR=/path/to/ida or the IDA_DIR environment variable.

if(NOT DEFINED IDA_DIR)
  if(DEFINED ENV{IDA_DIR})
    set(IDA_DIR "$ENV{IDA_DIR}")
  else()
    set(IDA_DIR "/home/fk/ida-pro-9.0")
  endif()
endif()

set(IDA_INCLUDE_DIR "${IDA_DIR}/include")
set(IDA_LIB_DIR     "${IDA_DIR}/lib/x64_linux_gcc_64")
set(IDA_LIB         "${IDA_LIB_DIR}/libida.so")

if(NOT EXISTS "${IDA_INCLUDE_DIR}/hexrays.hpp")
  message(FATAL_ERROR "hexrays.hpp not found under ${IDA_INCLUDE_DIR}. Set -DIDA_DIR=...")
endif()
if(NOT EXISTS "${IDA_LIB}")
  message(FATAL_ERROR "libida.so not found at ${IDA_LIB}. Set -DIDA_DIR=...")
endif()

# Compile definitions required by the IDA SDK on 64-bit Linux.
set(IDA_DEFINITIONS __LINUX__ __EA64__)

# Helper: turn a target into an IDA plugin shared object (<name>.so, no lib prefix).
function(ida_configure_plugin tgt)
  target_include_directories(${tgt} PRIVATE "${IDA_INCLUDE_DIR}")
  target_compile_definitions(${tgt} PRIVATE ${IDA_DEFINITIONS})
  target_link_libraries(${tgt} PRIVATE "${IDA_LIB}")
  set_target_properties(${tgt} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${tgt}"
    POSITION_INDEPENDENT_CODE ON)
endfunction()
