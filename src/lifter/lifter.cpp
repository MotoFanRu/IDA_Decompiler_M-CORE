#include "lifter/lifter.h"

#include "ir/ir.h"

#include <ida.hpp>
#include <idp.hpp>
#include <ua.hpp>
#include <funcs.hpp>
#include <name.hpp>
#include <lines.hpp>

// Instruction- and register-id enums from the vendored M*CORE module. The values
// must match the loaded procs/mcore.so (same source), which they do.
#include "ins.hpp"     // nameNum: mcore_movi, mcore_jmp, ...
#include "mcore.hpp"   // mcore_registers: rR2 (=2), rR15 (=15), ...

namespace mcore {

namespace {

ir::Stmt lift_unknown(ea_t ea) {
  qstring line;
  generate_disasm_line(&line, ea, GENDSM_REMOVE_TAGS);
  return ir::unknown((uint32_t)ea, line.c_str());
}

} // namespace

bool lift_function(func_t *pfn, ir::Function &out, qstring &err) {
  out = ir::Function{};
  err.clear();
  if (pfn == nullptr) {
    err = "no function";
    return false;
  }

  out.entry = (uint32_t)pfn->start_ea;
  qstring nm;
  get_func_name(&nm, pfn->start_ea);
  out.name = nm.c_str();

  for (ea_t ea = pfn->start_ea; ea < pfn->end_ea;) {
    insn_t insn;
    int len = decode_insn(&insn, ea);
    if (len <= 0) {
      err.sprnt("decode failed at %a", ea);
      return false;
    }

    const int d = insn.ops[0].reg;             // ops[0] = dest (also src for ALU)
    const int s = insn.ops[1].reg;             // ops[1] = src reg (reg-reg ops)
    const int64_t imm = (int64_t)insn.ops[1].value;  // ops[1] = imm (imm ops)
    auto reg_reg = [&](ir::BinOp op) {
      return ir::assign(d, ir::binop(op, ir::reg(d), ir::reg(s)), (uint32_t)ea);
    };
    auto reg_imm = [&](ir::BinOp op) {
      return ir::assign(d, ir::binop(op, ir::reg(d), ir::constant(imm)), (uint32_t)ea);
    };

    switch (insn.itype) {
      case mcore_movi:  // dst = imm
        out.stmts.push_back(ir::assign(d, ir::constant(imm), (uint32_t)ea));
        break;
      case mcore_mov:   // dst = src
        out.stmts.push_back(ir::assign(d, ir::reg(s), (uint32_t)ea));
        break;

      case mcore_addu: out.stmts.push_back(reg_reg(ir::BinOp::Add)); break;
      case mcore_subu: out.stmts.push_back(reg_reg(ir::BinOp::Sub)); break;
      case mcore_and:  out.stmts.push_back(reg_reg(ir::BinOp::And)); break;
      case mcore_or:   out.stmts.push_back(reg_reg(ir::BinOp::Or));  break;
      case mcore_xor:  out.stmts.push_back(reg_reg(ir::BinOp::Xor)); break;
      case mcore_rsub: // dst = src - dst
        out.stmts.push_back(ir::assign(
            d, ir::binop(ir::BinOp::Sub, ir::reg(s), ir::reg(d)), (uint32_t)ea));
        break;

      case mcore_addi: out.stmts.push_back(reg_imm(ir::BinOp::Add)); break;
      case mcore_subi: out.stmts.push_back(reg_imm(ir::BinOp::Sub)); break;
      case mcore_lsli: out.stmts.push_back(reg_imm(ir::BinOp::Shl)); break;
      case mcore_lsri: out.stmts.push_back(reg_imm(ir::BinOp::Shr)); break;
      case mcore_asri: out.stmts.push_back(reg_imm(ir::BinOp::Sar)); break;

      case mcore_jmp:
        if (insn.ops[0].reg == ir::kRegLR) {
          // jmp r15 == rts: the ABI return value lives in r2.
          out.stmts.push_back(ir::ret(ir::reg(ir::kRegRet), (uint32_t)ea));
        } else {
          out.stmts.push_back(lift_unknown(ea));
        }
        break;

      default:
        out.stmts.push_back(lift_unknown(ea));
        break;
    }

    ea += len;
  }

  return true;
}

} // namespace mcore
