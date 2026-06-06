// Offline unit tests for the M-CORE decoder.
//
// M0: harness sanity + the decoder's length/guard contract. Real per-opcode
// decoding tests land in M2 (one instruction group at a time, TDD).
#include "test_framework.h"

#include "decoder/mcore_insn.h"

using namespace mcore;

TEST("harness sanity") {
  CHECK(true);
  CHECK_EQ(1 + 1, 2);
}

TEST("decode rejects insufficient input") {
  Insn insn;
  uint8_t one_byte[1] = {0x00};
  CHECK(!decode(one_byte, 0, 0x1000, insn));
  CHECK(!decode(one_byte, 1, 0x1000, insn));
  CHECK(!decode(nullptr, 2, 0x1000, insn));
}

TEST("decode consumes a 16-bit halfword") {
  Insn insn;
  uint8_t bytes[2] = {0x00, 0x00};
  CHECK(decode(bytes, sizeof(bytes), 0x1234, insn));
  CHECK_EQ((int)insn.size, 2);
  CHECK_EQ((int)insn.ea, 0x1234);
}

TEST_MAIN()
