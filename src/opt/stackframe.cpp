// Stack-frame recovery: sp-relative memory accesses become named stack-slot
// variables; prologue/epilogue boilerplate is removed. Pure, unit-testable.
#include "opt/opt.h"

#include "ir/ir.h"

#include <vector>

namespace opt {

namespace {

bool is_sp(const ir::ExprPtr &e) {
  return e && e->kind == ir::ExprKind::Reg && e->reg == ir::kRegSP;
}

// If `addr` is `sp` or `sp + const`, return true and set the byte offset.
bool sp_offset(const ir::ExprPtr &addr, int64_t &off) {
  if (is_sp(addr)) { off = 0; return true; }
  if (addr && addr->kind == ir::ExprKind::BinOp && addr->binop == ir::BinOp::Add &&
      is_sp(addr->a) && addr->b && addr->b->kind == ir::ExprKind::Const) {
    off = addr->b->value;
    return true;
  }
  return false;
}

// Replace sp-relative loads with stack-slot register references.
ir::ExprPtr rw(const ir::ExprPtr &e) {
  if (!e) return e;
  switch (e->kind) {
    case ir::ExprKind::Load: {
      int64_t off;
      if (sp_offset(e->a, off)) return ir::reg(ir::kStackBase + (int)off);
      return ir::load(rw(e->a), e->size);
    }
    case ir::ExprKind::BinOp: return ir::binop(e->binop, rw(e->a), rw(e->b));
    case ir::ExprKind::UnOp:  return ir::unop(e->unop, rw(e->a));
    case ir::ExprKind::Cast:  return ir::cast(e->size, e->is_signed, rw(e->a));
    case ir::ExprKind::Call: {
      std::vector<ir::ExprPtr> args;
      for (const auto &x : e->args) args.push_back(rw(x));
      return e->a ? ir::call_indirect(rw(e->a), std::move(args))
                  : ir::call(e->name, std::move(args));
    }
    default: return e;
  }
}

bool is_lr(const ir::ExprPtr &e) {
  return e && e->kind == ir::ExprKind::Reg && e->reg == ir::kRegLR;
}

bool is_frame_multi(const ir::Stmt &s) {
  if (s.kind != ir::StmtKind::Unknown) return false;
  return s.text.rfind("stm", 0) == 0 || s.text.rfind("ldm", 0) == 0;
}

} // namespace

void recover_stack(ir::Function &fn) {
  for (auto &b : fn.blocks) {
    std::vector<ir::Stmt> kept;
    kept.reserve(b.stmts.size());
    for (auto &s : b.stmts) {
      // Drop prologue/epilogue register save/restore (stm/ldm at sp).
      if (is_frame_multi(s)) continue;

      if (s.kind == ir::StmtKind::Store) {
        int64_t off;
        if (sp_offset(s.addr, off)) {
          // *(sp+off) = v   becomes   var_off = v
          if (is_lr(s.expr)) continue;  // saving lr: drop
          kept.push_back(ir::assign(ir::kStackBase + (int)off, rw(s.expr), s.ea));
          continue;
        }
        s.addr = rw(s.addr);
        s.expr = rw(s.expr);
        kept.push_back(std::move(s));
        continue;
      }

      if (s.kind == ir::StmtKind::Assign) {
        s.expr = rw(s.expr);
        if (s.dst_reg == ir::kRegSP) continue;   // sp adjustment: drop
        if (s.dst_reg == ir::kRegLR) continue;   // lr restore: drop
        if (is_lr(s.expr)) continue;             // saving lr into a slot/reg: drop
        kept.push_back(std::move(s));
        continue;
      }

      kept.push_back(std::move(s));
    }
    b.stmts = std::move(kept);

    // Rewrite terminator operands too (e.g. a return value loaded from the frame).
    b.term.cond = rw(b.term.cond);
    b.term.value = rw(b.term.value);
  }
}

} // namespace opt
