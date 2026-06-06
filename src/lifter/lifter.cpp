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

// Instruction- and register-id enums from the vendored M*CORE module.
#include "ins.hpp"     // nameNum: mcore_movi, mcore_addu, mcore_bt, ...
#include "mcore.hpp"   // mcore_registers

namespace mcore {

namespace {

ir::Stmt lift_unknown(ea_t ea) {
  qstring line;
  generate_disasm_line(&line, ea, GENDSM_REMOVE_TAGS);
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

    default: blk.stmts.push_back(lift_unknown((ea_t)ea)); break;
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
