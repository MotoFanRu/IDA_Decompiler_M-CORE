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

    switch (insn.itype) {
      case mcore_movi:
        // ops[0] = dst reg, ops[1] = immediate value
        out.stmts.push_back(ir::assign(
            insn.ops[0].reg, ir::constant((int64_t)insn.ops[1].value),
            (uint32_t)ea));
        break;

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
