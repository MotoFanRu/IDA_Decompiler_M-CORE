#include "opt/opt.h"

#include "ir/ir.h"

#include <map>

namespace opt {

namespace {

bool is_const(const ir::ExprPtr &e) {
  return e && e->kind == ir::ExprKind::Const;
}

// Fold a BinOp of two constants. Returns nullptr if not foldable.
ir::ExprPtr fold_binop(ir::BinOp op, int64_t a, int64_t b) {
  switch (op) {
    case ir::BinOp::Add: return ir::constant(a + b);
    case ir::BinOp::Sub: return ir::constant(a - b);
    case ir::BinOp::Mul: return ir::constant(a * b);
    case ir::BinOp::And: return ir::constant(a & b);
    case ir::BinOp::Or:  return ir::constant(a | b);
    case ir::BinOp::Xor: return ir::constant(a ^ b);
    case ir::BinOp::Shl: return ir::constant(a << (b & 31));
    case ir::BinOp::Shr: return ir::constant((int64_t)((uint64_t)(uint32_t)a >> (b & 31)));
    case ir::BinOp::Sar: return ir::constant((int32_t)a >> (b & 31));
    case ir::BinOp::CmpLt: return ir::constant(a < b ? 1 : 0);
    case ir::BinOp::CmpHs: return ir::constant((uint32_t)a >= (uint32_t)b ? 1 : 0);
    case ir::BinOp::CmpNe: return ir::constant(a != b ? 1 : 0);
    case ir::BinOp::CmpEq: return ir::constant(a == b ? 1 : 0);
  }
  return nullptr;
}

// Return a simplified copy of `e` with registers replaced by their known
// definitions and constant operations folded.
ir::ExprPtr rewrite(const ir::ExprPtr &e, const std::map<int, ir::ExprPtr> &defs) {
  if (!e) return e;
  switch (e->kind) {
    case ir::ExprKind::Const:
      return e;
    case ir::ExprKind::Reg: {
      auto it = defs.find(e->reg);
      return it != defs.end() ? it->second : e;
    }
    case ir::ExprKind::UnOp: {
      ir::ExprPtr a = rewrite(e->a, defs);
      if (is_const(a)) {
        int64_t v = a->value;
        return ir::constant(e->unop == ir::UnOp::Neg ? -v : ~v);
      }
      return ir::unop(e->unop, a);
    }
    case ir::ExprKind::BinOp: {
      ir::ExprPtr a = rewrite(e->a, defs);
      ir::ExprPtr b = rewrite(e->b, defs);
      if (is_const(a) && is_const(b)) {
        if (ir::ExprPtr folded = fold_binop(e->binop, a->value, b->value))
          return folded;
      }
      return ir::binop(e->binop, a, b);
    }
  }
  return e;
}

bool uses_reg(const ir::ExprPtr &e, int reg) {
  if (!e) return false;
  switch (e->kind) {
    case ir::ExprKind::Const: return false;
    case ir::ExprKind::Reg:   return e->reg == reg;
    case ir::ExprKind::UnOp:  return uses_reg(e->a, reg);
    case ir::ExprKind::BinOp: return uses_reg(e->a, reg) || uses_reg(e->b, reg);
  }
  return false;
}

} // namespace

void simplify(ir::Function &fn) {
  // Forward const/copy propagation + folding.
  std::map<int, ir::ExprPtr> defs;
  for (auto &s : fn.stmts) {
    switch (s.kind) {
      case ir::StmtKind::Assign:
        s.expr = rewrite(s.expr, defs);
        defs[s.dst_reg] = s.expr;
        break;
      case ir::StmtKind::Return:
        s.expr = rewrite(s.expr, defs);
        break;
      case ir::StmtKind::Unknown:
        // Unknown side effects: drop all known definitions.
        defs.clear();
        break;
    }
  }

  // Dead-assignment elimination: drop an Assign whose dst is not read by any
  // later statement. (Single-block conservative form; CFG liveness comes later.)
  std::vector<ir::Stmt> kept;
  kept.reserve(fn.stmts.size());
  for (size_t i = 0; i < fn.stmts.size(); ++i) {
    const ir::Stmt &s = fn.stmts[i];
    if (s.kind == ir::StmtKind::Assign) {
      bool used = false;
      for (size_t j = i + 1; j < fn.stmts.size() && !used; ++j)
        used = uses_reg(fn.stmts[j].expr, s.dst_reg);
      if (!used)
        continue;
    }
    kept.push_back(s);
  }
  fn.stmts = std::move(kept);
}

} // namespace opt
