// Internal representation of a decoded M-CORE instruction.
//
// This header is intentionally free of any IDA/Hex-Rays dependency so the
// decoder can be unit-tested offline. The lifter (which does depend on
// Hex-Rays) consumes these structures.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mcore {

// Opcode set grows milestone by milestone (M2+). Only the cases the decoder
// can currently produce are listed; everything else decodes as Unknown.
enum class Op : uint16_t {
  Unknown = 0,
};

enum class OperandKind : uint8_t {
  None = 0,
  Reg,        // general register r0..r15
  Imm,        // immediate constant
  MemRegImm,  // [reg + scaled imm]  (load/store)
};

struct Operand {
  OperandKind kind = OperandKind::None;
  uint8_t reg = 0;     // register number for Reg / base of MemRegImm
  int64_t imm = 0;     // immediate / offset
};

struct Insn {
  uint32_t ea = 0;     // address of the instruction
  uint8_t size = 0;    // length in bytes (0 = decode failed)
  Op op = Op::Unknown;
  Operand ops[3];      // up to three operands
  bool reads_C = false;
  bool writes_C = false;
};

// Decode a single M-CORE instruction from `bytes` (with `len` bytes available)
// located at address `ea`. Endianness is little-endian for the instruction
// halfword. Returns true on success and fills `out`; returns false if there
// are not enough bytes to decode an instruction.
bool decode(const uint8_t *bytes, size_t len, uint32_t ea, Insn &out);

} // namespace mcore
