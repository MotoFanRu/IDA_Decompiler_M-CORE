#include "lifter/lifter.h"

#include "ir/ir.h"

#include <ida.hpp>
#include <idp.hpp>
#include <ua.hpp>
#include <bytes.hpp>
#include <funcs.hpp>
#include <gdl.hpp>
#include <name.hpp>
#include <lines.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcore {

namespace {

// Processor-module instruction numbers are private implementation details. In
// particular, IDA 9.4's built-in MCORE module uses a different itype ordering
// than the former third-party module. Resolve canonical SDK mnemonics to the
// lifter's stable local identifiers instead.
enum class McoreInsn {
  Unknown,
  Abs, Addc, Addi, Addu, And, Andi, Andn, Asr, Asri,
  Bclri, Bf, Bgeni, Bgenr, Bmaski, Br, Brev, Bseti, Bsr, Bt, Btsti,
  Clrf, Clrt, Cmphs, Cmplt, Cmplti, Cmpne, Cmpnei,
  Decf, Decgt, Declt, Decne, Dect, Divs, Divu,
  Ff1, Incf, Inct, Ixh, Ixw,
  Jmp, Jmpi, Jsr, Jsri,
  Ld, LdB, LdH, Ldq, Lrw, Lsl, Lsli, Lsr, Lsri,
  Mov, Movf, Movi, Movt, Mult, Mvc, Mvcv,
  Not, Or, Rotli, Rsub, Rsubi,
  Sextb, Sexth, St, StB, StH, Stq,
  Subc, Subi, Subu, Tst, Xor,
  Xtrb0, Xtrb1, Xtrb2, Xtrb3, Zextb, Zexth,
};

McoreInsn classify(const insn_t &insn) {
  static const std::unordered_map<std::string_view, McoreInsn> kinds = {
      {"abs", McoreInsn::Abs},       {"addc", McoreInsn::Addc},
      {"addi", McoreInsn::Addi},     {"addu", McoreInsn::Addu},
      {"and", McoreInsn::And},       {"andi", McoreInsn::Andi},
      {"andn", McoreInsn::Andn},     {"asr", McoreInsn::Asr},
      {"asri", McoreInsn::Asri},     {"bclri", McoreInsn::Bclri},
      {"bf", McoreInsn::Bf},         {"bgeni", McoreInsn::Bgeni},
      {"bgenr", McoreInsn::Bgenr},   {"bmaski", McoreInsn::Bmaski},
      {"br", McoreInsn::Br},         {"brev", McoreInsn::Brev},
      {"bseti", McoreInsn::Bseti},   {"bsr", McoreInsn::Bsr},
      {"bt", McoreInsn::Bt},         {"btsti", McoreInsn::Btsti},
      {"clrf", McoreInsn::Clrf},     {"clrt", McoreInsn::Clrt},
      {"cmphs", McoreInsn::Cmphs},   {"cmplt", McoreInsn::Cmplt},
      {"cmplti", McoreInsn::Cmplti}, {"cmpne", McoreInsn::Cmpne},
      {"cmpnei", McoreInsn::Cmpnei}, {"decf", McoreInsn::Decf},
      {"decgt", McoreInsn::Decgt},   {"declt", McoreInsn::Declt},
      {"decne", McoreInsn::Decne},   {"dect", McoreInsn::Dect},
      {"divs", McoreInsn::Divs},     {"divu", McoreInsn::Divu},
      {"ff1", McoreInsn::Ff1},       {"incf", McoreInsn::Incf},
      {"inct", McoreInsn::Inct},     {"ixh", McoreInsn::Ixh},
      {"ixw", McoreInsn::Ixw},       {"jmp", McoreInsn::Jmp},
      {"jmpi", McoreInsn::Jmpi},     {"jsr", McoreInsn::Jsr},
      {"jsri", McoreInsn::Jsri},     {"ld", McoreInsn::Ld},
      {"ld.b", McoreInsn::LdB},      {"ld.h", McoreInsn::LdH},
      {"ldq", McoreInsn::Ldq},       {"lrw", McoreInsn::Lrw},
      {"lsl", McoreInsn::Lsl},       {"lsli", McoreInsn::Lsli},
      {"lsr", McoreInsn::Lsr},       {"lsri", McoreInsn::Lsri},
      {"mov", McoreInsn::Mov},       {"movf", McoreInsn::Movf},
      {"movi", McoreInsn::Movi},     {"movt", McoreInsn::Movt},
      {"mult", McoreInsn::Mult},     {"mvc", McoreInsn::Mvc},
      {"mvcv", McoreInsn::Mvcv},     {"not", McoreInsn::Not},
      {"or", McoreInsn::Or},         {"rotli", McoreInsn::Rotli},
      {"rsub", McoreInsn::Rsub},     {"rsubi", McoreInsn::Rsubi},
      {"sextb", McoreInsn::Sextb},   {"sexth", McoreInsn::Sexth},
      {"st", McoreInsn::St},         {"st.b", McoreInsn::StB},
      {"st.h", McoreInsn::StH},      {"stq", McoreInsn::Stq},
      {"subc", McoreInsn::Subc},     {"subi", McoreInsn::Subi},
      {"subu", McoreInsn::Subu},     {"tst", McoreInsn::Tst},
      {"xor", McoreInsn::Xor},       {"xtrb0", McoreInsn::Xtrb0},
      {"xtrb1", McoreInsn::Xtrb1},   {"xtrb2", McoreInsn::Xtrb2},
      {"xtrb3", McoreInsn::Xtrb3},   {"zextb", McoreInsn::Zextb},
      {"zexth", McoreInsn::Zexth},
  };
  const char *mnem = insn.get_canon_mnem(PH);
  if (mnem == nullptr) return McoreInsn::Unknown;
  auto it = kinds.find(std::string_view(mnem));
  return it == kinds.end() ? McoreInsn::Unknown : it->second;
}

ir::Stmt lift_unknown(ea_t ea) {
  qstring line;
  generate_disasm_line(&line, ea, GENDSM_REMOVE_TAGS);
  line.trim2();  // collapse padding so the __asm text is tidy
  return ir::unknown((uint32_t)ea, line.c_str());
}

// Lift a single non-control instruction into `blk` statements. Returns true if
// handled (incl. as an Unknown placeholder); control-flow is handled separately.
void lift_insn(const insn_t &insn, ir::Block &blk) {
  const int d = insn.ops[0].reg;
  const int s = insn.ops[1].reg;
  const int64_t imm = (int64_t)insn.ops[1].value;
  const uint32_t ea = (uint32_t)insn.ea;
  auto rr = [&](ir::BinOp op) {
    blk.stmts.push_back(ir::assign(d, ir::binop(op, ir::reg(d), ir::reg(s)), ea));
  };
  auto ri = [&](ir::BinOp op) {
    blk.stmts.push_back(ir::assign(d, ir::binop(op, ir::reg(d), ir::constant(imm)), ea));
  };
  auto setc = [&](ir::BinOp cmp, ir::ExprPtr rhs) {
    blk.stmts.push_back(ir::assign(ir::kRegC, ir::binop(cmp, ir::reg(d), std::move(rhs)), ea));
  };
  // Memory operand (ops[1]): base register in .phrase, byte offset in .addr.
  auto mem_addr = [&]() -> ir::ExprPtr {
    int base = insn.ops[1].phrase;
    int64_t off = (int64_t)insn.ops[1].addr;
    return off ? ir::binop(ir::BinOp::Add, ir::reg(base), ir::constant(off)) : ir::reg(base);
  };
  auto load_to = [&](int size) { blk.stmts.push_back(ir::assign(d, ir::load(mem_addr(), size), ea)); };
  auto store_from = [&](int size) { blk.stmts.push_back(ir::store(mem_addr(), ir::reg(d), size, ea)); };
  // Heuristic call arguments: contiguous arg regs r2.. set earlier in this block.
  auto call_args = [&]() -> std::vector<ir::ExprPtr> {
    std::set<int> defined;
    for (const auto &st : blk.stmts)
      if (st.kind == ir::StmtKind::Assign && st.dst_reg >= 2 && st.dst_reg <= 7)
        defined.insert(st.dst_reg);
    std::vector<ir::ExprPtr> args;
    for (int r = 2; r <= 7 && defined.count(r); ++r) args.push_back(ir::reg(r));
    return args;
  };

  switch (classify(insn)) {
    case McoreInsn::Movi: blk.stmts.push_back(ir::assign(d, ir::constant(imm), ea)); break;
    case McoreInsn::Mov:  blk.stmts.push_back(ir::assign(d, ir::reg(s), ea)); break;

    case McoreInsn::Addu: rr(ir::BinOp::Add); break;
    case McoreInsn::Subu: rr(ir::BinOp::Sub); break;
    case McoreInsn::Addc:  // d = d + s + C ; C = carry out  (C is the shared carry/condition bit)
      blk.stmts.push_back(ir::assign(d,
          ir::binop(ir::BinOp::Add, ir::binop(ir::BinOp::Add, ir::reg(d), ir::reg(s)), ir::reg(ir::kRegC)), ea));
      blk.stmts.push_back(ir::assign(ir::kRegC, ir::call("__carry", {ir::reg(d), ir::reg(s)}), ea));
      break;
    case McoreInsn::Subc:  // d = d - s - 1 + C ; C = not-borrow out
      blk.stmts.push_back(ir::assign(d,
          ir::binop(ir::BinOp::Add,
              ir::binop(ir::BinOp::Sub, ir::binop(ir::BinOp::Sub, ir::reg(d), ir::reg(s)), ir::constant(1)),
              ir::reg(ir::kRegC)), ea));
      blk.stmts.push_back(ir::assign(ir::kRegC, ir::call("__borrow", {ir::reg(d), ir::reg(s)}), ea));
      break;
    case McoreInsn::And: rr(ir::BinOp::And); break;
    case McoreInsn::Or:  rr(ir::BinOp::Or);  break;
    case McoreInsn::Xor: rr(ir::BinOp::Xor); break;
    case McoreInsn::Rsub:
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Sub, ir::reg(s), ir::reg(d)), ea));
      break;

    case McoreInsn::Addi: ri(ir::BinOp::Add); break;
    case McoreInsn::Subi: ri(ir::BinOp::Sub); break;
    case McoreInsn::Lsli: ri(ir::BinOp::Shl); break;
    case McoreInsn::Lsri: ri(ir::BinOp::Shr); break;
    case McoreInsn::Asri: ri(ir::BinOp::Sar); break;

    // Compares set the condition (C) bit.
    case McoreInsn::Cmplt: setc(ir::BinOp::CmpLt, ir::reg(s)); break;
    case McoreInsn::Cmphs: setc(ir::BinOp::CmpHs, ir::reg(s)); break;
    case McoreInsn::Cmpne: setc(ir::BinOp::CmpNe, ir::reg(s)); break;
    case McoreInsn::Cmplti: setc(ir::BinOp::CmpLt, ir::constant(imm)); break;
    case McoreInsn::Cmpnei: setc(ir::BinOp::CmpNe, ir::constant(imm)); break;

    case McoreInsn::Mult: rr(ir::BinOp::Mul); break;
    case McoreInsn::Andi: ri(ir::BinOp::And); break;
    case McoreInsn::Rsubi:  // d = imm - d
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Sub, ir::constant(imm), ir::reg(d)), ea));
      break;
    case McoreInsn::Decne:  // d = d - 1 ; C = (d != 0)
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Sub, ir::reg(d), ir::constant(1)), ea));
      blk.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpNe, ir::reg(d), ir::constant(0)), ea));
      break;

    // bgeni d = 1<<n ; bmaski d = (1<<n)-1 (n==0 means 32 -> all ones).
    // Read the 5-bit field directly so the result does not depend on how a
    // processor module chooses to expose edge-case immediates.
    case McoreInsn::Bgeni: {
      uint32_t w = ((uint32_t)get_byte((ea_t)ea) << 8) | get_byte((ea_t)ea + 1);
      int n = (w >> 4) & 0x1f;
      blk.stmts.push_back(ir::assign(d, ir::constant((int64_t)(uint32_t)(1u << n)), ea));
      break;
    }
    case McoreInsn::Bmaski: {
      uint32_t w = ((uint32_t)get_byte((ea_t)ea) << 8) | get_byte((ea_t)ea + 1);
      int n = (w >> 4) & 0x1f;
      uint32_t mask = (n == 0) ? 0xFFFFFFFFu : ((1u << n) - 1);
      blk.stmts.push_back(ir::assign(d, ir::constant((int64_t)mask), ea));
      break;
    }
    case McoreInsn::Ixw:  // d = d + s*4
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Add, ir::reg(d),
          ir::binop(ir::BinOp::Mul, ir::reg(s), ir::constant(4))), ea));
      break;
    case McoreInsn::Ixh:  // d = d + s*2
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Add, ir::reg(d),
          ir::binop(ir::BinOp::Mul, ir::reg(s), ir::constant(2))), ea));
      break;

    // Bit operations (imm = bit number).
    case McoreInsn::Bseti:  // d |= 1<<n
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Or, ir::reg(d),
          ir::constant((int64_t)(1u << imm))), ea));
      break;
    case McoreInsn::Bclri:  // d &= ~(1<<n)
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::And, ir::reg(d),
          ir::constant((int64_t)(uint32_t)~(1u << imm))), ea));
      break;
    case McoreInsn::Mvc:   // d = C
      blk.stmts.push_back(ir::assign(d, ir::reg(ir::kRegC), ea));
      break;
    case McoreInsn::Mvcv:  // d = !C
      blk.stmts.push_back(ir::assign(d, ir::unop(ir::UnOp::LNot, ir::reg(ir::kRegC)), ea));
      break;

    case McoreInsn::Btsti:  // C = (d >> n) & 1
      blk.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::And,
          ir::binop(ir::BinOp::Shr, ir::reg(d), ir::constant(imm)), ir::constant(1)), ea));
      break;

    // Sign/zero extensions -> readable casts.
    case McoreInsn::Zextb: blk.stmts.push_back(ir::assign(d, ir::cast(1, false, ir::reg(d)), ea)); break;
    case McoreInsn::Sextb: blk.stmts.push_back(ir::assign(d, ir::cast(1, true,  ir::reg(d)), ea)); break;
    case McoreInsn::Zexth: blk.stmts.push_back(ir::assign(d, ir::cast(2, false, ir::reg(d)), ea)); break;
    case McoreInsn::Sexth: blk.stmts.push_back(ir::assign(d, ir::cast(2, true,  ir::reg(d)), ea)); break;

    // Load relative word: a 32-bit value (constant or resolved symbol) from the
    // literal pool. IDA puts the loaded value in ops[1].addr.
    case McoreInsn::Lrw: {
      int64_t v = (int64_t)insn.ops[1].addr;
      qstring nm;
      if (get_name(&nm, (ea_t)v) > 0 && !nm.empty())
        blk.stmts.push_back(ir::assign(d, ir::const_named(v, nm.c_str()), ea));
      else
        blk.stmts.push_back(ir::assign(d, ir::constant((int64_t)(uint32_t)v), ea));
      break;
    }
    // Indirect call via the literal pool; IDA resolves the target in ops[0].addr.
    case McoreInsn::Jsri: {
      ea_t tgt = (ea_t)insn.ops[0].addr;
      qstring nm;
      if (get_name(&nm, tgt) <= 0 || nm.empty()) nm.sprnt("sub_%X", (unsigned)tgt);
      blk.stmts.push_back(ir::assign(ir::kRegRet, ir::call(nm.c_str(), call_args()), ea));
      break;
    }

    case McoreInsn::Divs:  // M-CORE divides by the implicit divisor register r1
    case McoreInsn::Divu:
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Div, ir::reg(d), ir::reg(1)), ea));
      break;
    case McoreInsn::Lsl:  rr(ir::BinOp::Shl); break;  // variable shifts
    case McoreInsn::Lsr:  rr(ir::BinOp::Shr); break;
    case McoreInsn::Asr:  rr(ir::BinOp::Sar); break;
    case McoreInsn::Bgenr:  // d = 1 << s
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Shl, ir::constant(1), ir::reg(s)), ea));
      break;
    case McoreInsn::Tst:    // C = (d & s) != 0
      blk.stmts.push_back(ir::assign(ir::kRegC,
          ir::binop(ir::BinOp::CmpNe, ir::binop(ir::BinOp::And, ir::reg(d), ir::reg(s)), ir::constant(0)), ea));
      break;

    // Conditional moves (read C): d = C ? then : else.
    case McoreInsn::Movt:  // if C: d = s
      blk.stmts.push_back(ir::assign(d, ir::select(ir::reg(ir::kRegC), ir::reg(s), ir::reg(d)), ea)); break;
    case McoreInsn::Movf:  // if !C: d = s
      blk.stmts.push_back(ir::assign(d, ir::select(ir::reg(ir::kRegC), ir::reg(d), ir::reg(s)), ea)); break;
    case McoreInsn::Clrt:  // if C: d = 0
      blk.stmts.push_back(ir::assign(d, ir::select(ir::reg(ir::kRegC), ir::constant(0), ir::reg(d)), ea)); break;
    case McoreInsn::Clrf:  // if !C: d = 0
      blk.stmts.push_back(ir::assign(d, ir::select(ir::reg(ir::kRegC), ir::reg(d), ir::constant(0)), ea)); break;
    case McoreInsn::Inct:  // if C: d = d + 1
      blk.stmts.push_back(ir::assign(d, ir::select(ir::reg(ir::kRegC),
          ir::binop(ir::BinOp::Add, ir::reg(d), ir::constant(1)), ir::reg(d)), ea)); break;
    case McoreInsn::Decf:  // if !C: d = d - 1
      blk.stmts.push_back(ir::assign(d, ir::select(ir::reg(ir::kRegC), ir::reg(d),
          ir::binop(ir::BinOp::Sub, ir::reg(d), ir::constant(1))), ea)); break;
    case McoreInsn::Incf:  // if !C: d = d + 1
      blk.stmts.push_back(ir::assign(d, ir::select(ir::reg(ir::kRegC), ir::reg(d),
          ir::binop(ir::BinOp::Add, ir::reg(d), ir::constant(1))), ea)); break;
    case McoreInsn::Dect:  // if C: d = d - 1
      blk.stmts.push_back(ir::assign(d, ir::select(ir::reg(ir::kRegC),
          ir::binop(ir::BinOp::Sub, ir::reg(d), ir::constant(1)), ir::reg(d)), ea)); break;

    case McoreInsn::Andn:  // d = d & ~s
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::And, ir::reg(d),
          ir::unop(ir::UnOp::Not, ir::reg(s))), ea)); break;
    case McoreInsn::Not:  // d = ~d
      blk.stmts.push_back(ir::assign(d, ir::unop(ir::UnOp::Not, ir::reg(d)), ea)); break;
    case McoreInsn::Abs:   // d = d < 0 ? -d : d
      blk.stmts.push_back(ir::assign(d, ir::select(
          ir::binop(ir::BinOp::CmpLt, ir::reg(d), ir::constant(0)),
          ir::unop(ir::UnOp::Neg, ir::reg(d)), ir::reg(d)), ea)); break;
    case McoreInsn::Rotli: {  // rotate left by imm: (d << n) | (d >> (32 - n))
      int nn = (int)(imm & 31);
      ir::ExprPtr e = nn == 0 ? ir::reg(d)
          : ir::binop(ir::BinOp::Or, ir::binop(ir::BinOp::Shl, ir::reg(d), ir::constant(nn)),
                                     ir::binop(ir::BinOp::Shr, ir::reg(d), ir::constant(32 - nn)));
      blk.stmts.push_back(ir::assign(d, e, ea)); break;
    }
    case McoreInsn::Ff1:   // find-first-one -> intrinsic
      blk.stmts.push_back(ir::assign(d, ir::call("__ff1", {ir::reg(d)}), ea)); break;
    case McoreInsn::Brev:  // bit reverse -> intrinsic
      blk.stmts.push_back(ir::assign(d, ir::call("__brev", {ir::reg(d)}), ea)); break;
    case McoreInsn::Declt:  // d = d - 1 ; C = (d < 0)
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Sub, ir::reg(d), ir::constant(1)), ea));
      blk.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpLt, ir::reg(d), ir::constant(0)), ea));
      break;
    case McoreInsn::Decgt:  // d = d - 1 ; C = (d > 0)  (0 < d)
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Sub, ir::reg(d), ir::constant(1)), ea));
      blk.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpLt, ir::constant(0), ir::reg(d)), ea));
      break;
    // Extract byte N of d into r1.
    case McoreInsn::Xtrb0: blk.stmts.push_back(ir::assign(1, ir::binop(ir::BinOp::And, ir::reg(d), ir::constant(0xFF)), ea)); break;
    case McoreInsn::Xtrb1: blk.stmts.push_back(ir::assign(1, ir::binop(ir::BinOp::And, ir::binop(ir::BinOp::Shr, ir::reg(d), ir::constant(8)), ir::constant(0xFF)), ea)); break;
    case McoreInsn::Xtrb2: blk.stmts.push_back(ir::assign(1, ir::binop(ir::BinOp::And, ir::binop(ir::BinOp::Shr, ir::reg(d), ir::constant(16)), ir::constant(0xFF)), ea)); break;
    case McoreInsn::Xtrb3: blk.stmts.push_back(ir::assign(1, ir::binop(ir::BinOp::And, ir::binop(ir::BinOp::Shr, ir::reg(d), ir::constant(24)), ir::constant(0xFF)), ea)); break;

    // Memory: ops[0] = value/dest reg, ops[1] = (base, offset).
    case McoreInsn::Ld:   load_to(4); break;
    case McoreInsn::LdH: load_to(2); break;
    case McoreInsn::LdB: load_to(1); break;
    case McoreInsn::St:   store_from(4); break;
    case McoreInsn::StH: store_from(2); break;
    case McoreInsn::StB: store_from(1); break;

    // Load/store quad: transfer r4..r7 to/from [base], [base+4], [base+8], [base+12].
    case McoreInsn::Ldq:
      for (int k = 0; k < 4; ++k) {
        ir::ExprPtr addr = k ? ir::binop(ir::BinOp::Add, ir::reg(d), ir::constant(k * 4)) : ir::reg(d);
        blk.stmts.push_back(ir::assign(4 + k, ir::load(addr, 4), ea));
      }
      break;
    case McoreInsn::Stq:
      for (int k = 0; k < 4; ++k) {
        ir::ExprPtr addr = k ? ir::binop(ir::BinOp::Add, ir::reg(d), ir::constant(k * 4)) : ir::reg(d);
        blk.stmts.push_back(ir::store(addr, ir::reg(4 + k), 4, ea));
      }
      break;

    // Calls: result in r2 (ABI).
    case McoreInsn::Bsr: {
      ea_t tgt = (ea_t)insn.ops[0].addr;
      qstring nm;
      if (get_name(&nm, tgt) <= 0 || nm.empty()) nm.sprnt("sub_%X", (unsigned)tgt);
      blk.stmts.push_back(ir::assign(ir::kRegRet, ir::call(nm.c_str(), call_args()), ea));
      break;
    }
    case McoreInsn::Jsr:
      blk.stmts.push_back(
          ir::assign(ir::kRegRet, ir::call_indirect(ir::reg(insn.ops[0].reg), call_args()), ea));
      break;

    default: blk.stmts.push_back(lift_unknown((ea_t)ea)); break;
  }
}

// If `insn` is a control-flow instruction, set `blk.term` and return true.
bool lift_terminator(const insn_t &insn, ir::Block &blk, ea_t fallthrough) {
  ir::Terminator &t = blk.term;
  t.ea = (uint32_t)insn.ea;
  switch (classify(insn)) {
    case McoreInsn::Bt:
      t.kind = ir::TermKind::CondBranch;
      t.cond = ir::reg(ir::kRegC);
      t.target = (uint32_t)insn.ops[0].addr;
      t.fallthrough = (uint32_t)fallthrough;
      return true;
    case McoreInsn::Bf:
      t.kind = ir::TermKind::CondBranch;
      t.cond = ir::unop(ir::UnOp::LNot, ir::reg(ir::kRegC));
      t.target = (uint32_t)insn.ops[0].addr;
      t.fallthrough = (uint32_t)fallthrough;
      return true;
    case McoreInsn::Br:
      t.kind = ir::TermKind::Goto;
      t.target = (uint32_t)insn.ops[0].addr;
      return true;
    case McoreInsn::Jmpi: {  // tail jump through the literal pool: a tail call
      ea_t tgt = (ea_t)insn.ops[1].addr;
      qstring nm;
      if (get_name(&nm, tgt) <= 0 || nm.empty()) nm.sprnt("sub_%X", (unsigned)tgt);
      std::vector<ir::ExprPtr> args;
      std::set<int> defined;
      for (const auto &st : blk.stmts)
        if (st.kind == ir::StmtKind::Assign && st.dst_reg >= 2 && st.dst_reg <= 7)
          defined.insert(st.dst_reg);
      for (int r = 2; r <= 7 && defined.count(r); ++r) args.push_back(ir::reg(r));
      t.kind = ir::TermKind::Return;
      t.value = ir::call(nm.c_str(), std::move(args));
      t.has_value = true;
      return true;
    }
    case McoreInsn::Jmp:
      t.kind = ir::TermKind::Return;
      if (insn.ops[0].reg == ir::kRegLR) {  // jmp r15 == rts
        t.value = ir::reg(ir::kRegRet);
      } else {  // jmp rX: indirect tail call/return
        std::set<int> defined;
        for (const auto &st : blk.stmts)
          if (st.kind == ir::StmtKind::Assign && st.dst_reg >= 2 && st.dst_reg <= 7)
            defined.insert(st.dst_reg);
        std::vector<ir::ExprPtr> args;
        for (int r = 2; r <= 7 && defined.count(r); ++r) args.push_back(ir::reg(r));
        t.value = ir::call_indirect(ir::reg(insn.ops[0].reg), std::move(args));
      }
      t.has_value = true;
      return true;
    default:
      return false;
  }
}

} // namespace

bool lift_function(func_t *pfn, ir::Function &out, qstring &err) {
  out = ir::Function{};
  err.clear();
  if (pfn == nullptr) { err = "no function"; return false; }

  out.entry = (uint32_t)pfn->start_ea;
  qstring nm;
  get_func_name(&nm, pfn->start_ea);
  out.name = nm.c_str();

  qflow_chart_t fc;
  fc.create("mc", pfn, pfn->start_ea, pfn->end_ea, 0);

  for (int i = 0; i < fc.size(); ++i) {
    ea_t b0 = fc.blocks[i].start_ea;
    ea_t b1 = fc.blocks[i].end_ea;

    ir::Block blk;
    blk.entry = (uint32_t)b0;
    blk.term.kind = ir::TermKind::Fallthrough;
    blk.term.target = (uint32_t)b1;  // default: fall to the next block

    for (ea_t ea = b0; ea < b1;) {
      insn_t insn;
      int len = decode_insn(&insn, ea);
      if (len <= 0) { err.sprnt("decode failed at %a", ea); return false; }
      const bool is_last = (ea + len >= b1);
      if (!(is_last && lift_terminator(insn, blk, ea + len)))
        lift_insn(insn, blk);
      ea += len;
    }

    out.blocks.push_back(std::move(blk));
  }

  // Entry block first (then ascending), so the structurer starts at index 0.
  std::sort(out.blocks.begin(), out.blocks.end(),
            [&](const ir::Block &a, const ir::Block &b) {
              if (a.entry == out.entry) return true;
              if (b.entry == out.entry) return false;
              return a.entry < b.entry;
            });

  // Drop blocks unreachable from the entry (e.g. trailing self-loop fragments
  // after a return/tail-call) so they don't force the goto fallback.
  if (!out.blocks.empty()) {
    std::map<uint32_t, int> idx;
    for (size_t i = 0; i < out.blocks.size(); ++i) idx[out.blocks[i].entry] = (int)i;
    std::vector<bool> reach(out.blocks.size(), false);
    std::vector<int> stack{0};
    reach[0] = true;
    while (!stack.empty()) {
      int i = stack.back(); stack.pop_back();
      const ir::Terminator &t = out.blocks[i].term;
      auto go = [&](uint32_t ea) {
        auto it = idx.find(ea);
        if (it != idx.end() && !reach[it->second]) { reach[it->second] = true; stack.push_back(it->second); }
      };
      if (t.kind == ir::TermKind::Goto || t.kind == ir::TermKind::Fallthrough) go(t.target);
      if (t.kind == ir::TermKind::CondBranch) { go(t.target); go(t.fallthrough); }
    }
    std::vector<ir::Block> kept;
    for (size_t i = 0; i < out.blocks.size(); ++i)
      if (reach[i]) kept.push_back(std::move(out.blocks[i]));
    out.blocks = std::move(kept);
  }

  return true;
}

} // namespace mcore
