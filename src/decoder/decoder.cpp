// M-CORE instruction decoder (pure, no IDA dependency).
//
// M0: skeleton only. Real opcode decoding is implemented in M2 via TDD,
// one instruction group at a time, against offline byte fixtures.
#include "decoder/mcore_insn.h"

namespace mcore {

bool decode(const uint8_t *bytes, size_t len, uint32_t ea, Insn &out) {
  if (bytes == nullptr || len < 2)
    return false;

  out = Insn{};
  out.ea = ea;
  out.size = 2;          // M-CORE base instructions are 16-bit
  out.op = Op::Unknown;  // filled in by later milestones
  return true;
}

} // namespace mcore
