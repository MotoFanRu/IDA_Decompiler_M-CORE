#include "lifter/lifter.h"

#include "ir/ir.h"

#include <ida.hpp>
#include <idp.hpp>
#include <ua.hpp>
#include <funcs.hpp>
#include <gdl.hpp>
#include <name.hpp>
#include <lines.hpp>

#include <algorithm>
#include <set>
#include <vector>

// Instruction- and register-id enums from the vendored M*CORE module.
#include "ins.hpp"     // nameNum: mcore_movi, mcore_addu, mcore_bt, ...
#include "mcore.hpp"   // mcore_registers

namespace mcore {

namespace {

ir::Stmt lift_unknown(ea_t ea, int itype = -1) {
  qstring line;
  generate_disasm_line(&line, ea, GENDSM_REMOVE_TAGS);
  if (itype >= 0) line.cat_sprnt("   [itype=%d]", itype);
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

  switch (insn.itype) {
    case mcore_movi: blk.stmts.push_back(ir::assign(d, ir::constant(imm), ea)); break;
    case mcore_mov:  blk.stmts.push_back(ir::assign(d, ir::reg(s), ea)); break;

    case mcore_addu: rr(ir::BinOp::Add); break;
    case mcore_subu: rr(ir::BinOp::Sub); break;
    case mcore_and:  rr(ir::BinOp::And); break;
    case mcore_or:   rr(ir::BinOp::Or);  break;
    case mcore_xor:  rr(ir::BinOp::Xor); break;
    case mcore_rsub:
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Sub, ir::reg(s), ir::reg(d)), ea));
      break;

    case mcore_addi: ri(ir::BinOp::Add); break;
    case mcore_subi: ri(ir::BinOp::Sub); break;
    case mcore_lsli: ri(ir::BinOp::Shl); break;
    case mcore_lsri: ri(ir::BinOp::Shr); break;
    case mcore_asri: ri(ir::BinOp::Sar); break;

    // Compares set the condition (C) bit.
    case mcore_cmplt: setc(ir::BinOp::CmpLt, ir::reg(s)); break;
    case mcore_cmphs: setc(ir::BinOp::CmpHs, ir::reg(s)); break;
    case mcore_cmpne: setc(ir::BinOp::CmpNe, ir::reg(s)); break;
    case mcore_cmplti: setc(ir::BinOp::CmpLt, ir::constant(imm)); break;
    case mcore_cmpnei: setc(ir::BinOp::CmpNe, ir::constant(imm)); break;

    case mcore_mult: rr(ir::BinOp::Mul); break;
    case mcore_andi: ri(ir::BinOp::And); break;
    case mcore_rsubi:  // d = imm - d
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Sub, ir::constant(imm), ir::reg(d)), ea));
      break;
    case mcore_decne:  // d = d - 1 ; C = (d != 0)
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Sub, ir::reg(d), ir::constant(1)), ea));
      blk.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpNe, ir::reg(d), ir::constant(0)), ea));
      break;

    // Bit generate (d = 1<<n). Only the variants that expose the immediate;
    // the base bgeni/bmaski encode it in the opcode (not surfaced) -> still TODO.
    case mcore_bgeni_0:
    case mcore_bgeni_1:
      blk.stmts.push_back(ir::assign(d, ir::constant((int64_t)(1u << imm)), ea));
      break;
    case mcore_ixw:  // d = d + s*4
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Add, ir::reg(d),
          ir::binop(ir::BinOp::Mul, ir::reg(s), ir::constant(4))), ea));
      break;
    case mcore_ixh:  // d = d + s*2
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Add, ir::reg(d),
          ir::binop(ir::BinOp::Mul, ir::reg(s), ir::constant(2))), ea));
      break;

    // Bit operations (imm = bit number).
    case mcore_bseti:  // d |= 1<<n
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::Or, ir::reg(d),
          ir::constant((int64_t)(1u << imm))), ea));
      break;
    case mcore_bclri:  // d &= ~(1<<n)
      blk.stmts.push_back(ir::assign(d, ir::binop(ir::BinOp::And, ir::reg(d),
          ir::constant((int64_t)(uint32_t)~(1u << imm))), ea));
      break;
    case mcore_btsti:  // C = (d >> n) & 1
      blk.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::And,
          ir::binop(ir::BinOp::Shr, ir::reg(d), ir::constant(imm)), ir::constant(1)), ea));
      break;

    // Sign/zero extensions -> readable casts.
    case mcore_zextb: blk.stmts.push_back(ir::assign(d, ir::cast(1, false, ir::reg(d)), ea)); break;
    case mcore_sextb: blk.stmts.push_back(ir::assign(d, ir::cast(1, true,  ir::reg(d)), ea)); break;
    case mcore_zexth: blk.stmts.push_back(ir::assign(d, ir::cast(2, false, ir::reg(d)), ea)); break;
    case mcore_sexth: blk.stmts.push_back(ir::assign(d, ir::cast(2, true,  ir::reg(d)), ea)); break;

    // Load relative word: a 32-bit value (constant or resolved symbol) from the
    // literal pool. IDA puts the loaded value in ops[1].addr.
    case mcore_lrw: {
      int64_t v = (int64_t)insn.ops[1].addr;
      qstring nm;
      if (get_name(&nm, (ea_t)v) > 0 && !nm.empty())
        blk.stmts.push_back(ir::assign(d, ir::const_named(v, nm.c_str()), ea));
      else
        blk.stmts.push_back(ir::assign(d, ir::constant((int64_t)(uint32_t)v), ea));
      break;
    }
    // Indirect call via the literal pool; IDA resolves the target in ops[0].addr.
    case mcore_jsri: {
      ea_t tgt = (ea_t)insn.ops[0].addr;
      qstring nm;
      if (get_name(&nm, tgt) <= 0 || nm.empty()) nm.sprnt("sub_%X", (unsigned)tgt);
      blk.stmts.push_back(ir::assign(ir::kRegRet, ir::call(nm.c_str(), call_args()), ea));
      break;
    }

    // Memory: ops[0] = value/dest reg, ops[1] = (base, offset).
    case mcore_ld:   load_to(4); break;
    case mcore_ld_h: load_to(2); break;
    case mcore_ld_b: load_to(1); break;
    case mcore_st:   store_from(4); break;
    case mcore_st_h: store_from(2); break;
    case mcore_st_b: store_from(1); break;

    // Calls: result in r2 (ABI).
    case mcore_bsr: {
      ea_t tgt = (ea_t)insn.ops[0].addr;
      qstring nm;
      if (get_name(&nm, tgt) <= 0 || nm.empty()) nm.sprnt("sub_%X", (unsigned)tgt);
      blk.stmts.push_back(ir::assign(ir::kRegRet, ir::call(nm.c_str(), call_args()), ea));
      break;
    }
    case mcore_jsr:
      blk.stmts.push_back(
          ir::assign(ir::kRegRet, ir::call_indirect(ir::reg(insn.ops[0].reg), call_args()), ea));
      break;

    default: blk.stmts.push_back(lift_unknown((ea_t)ea, insn.itype)); break;
  }
}

// If `insn` is a control-flow instruction, set `blk.term` and return true.
bool lift_terminator(const insn_t &insn, ir::Block &blk, ea_t fallthrough) {
  ir::Terminator &t = blk.term;
  t.ea = (uint32_t)insn.ea;
  switch (insn.itype) {
    case mcore_bt:
      t.kind = ir::TermKind::CondBranch;
      t.cond = ir::reg(ir::kRegC);
      t.target = (uint32_t)insn.ops[0].addr;
      t.fallthrough = (uint32_t)fallthrough;
      return true;
    case mcore_bf:
      t.kind = ir::TermKind::CondBranch;
      t.cond = ir::unop(ir::UnOp::LNot, ir::reg(ir::kRegC));
      t.target = (uint32_t)insn.ops[0].addr;
      t.fallthrough = (uint32_t)fallthrough;
      return true;
    case mcore_br:
      t.kind = ir::TermKind::Goto;
      t.target = (uint32_t)insn.ops[0].addr;
      return true;
    case mcore_jmpi: {  // tail jump through the literal pool: a tail call
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
    case mcore_jmp:
      if (insn.ops[0].reg == ir::kRegLR) {  // jmp r15 == rts
        t.kind = ir::TermKind::Return;
        t.value = ir::reg(ir::kRegRet);
        t.has_value = true;
        return true;
      }
      return false;  // indirect jmp: handled later (B5+)
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

  return true;
}

} // namespace mcore
