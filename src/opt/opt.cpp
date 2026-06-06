#include "opt/opt.h"

#include "ir/ir.h"

#include <map>
#include <vector>

namespace opt {

namespace {

bool is_const(const ir::ExprPtr &e) {
  return e && e->kind == ir::ExprKind::Const;
}

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

// Replace registers by known constant defs and fold constant operations.
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
        switch (e->unop) {
          case ir::UnOp::Neg: return ir::constant(-v);
          case ir::UnOp::Not: return ir::constant(~v);
          case ir::UnOp::LNot: return ir::constant(v == 0 ? 1 : 0);
        }
      }
      return ir::unop(e->unop, a);
    }
    case ir::ExprKind::BinOp: {
      ir::ExprPtr a = rewrite(e->a, defs);
      ir::ExprPtr b = rewrite(e->b, defs);
      if (is_const(a) && is_const(b))
        if (ir::ExprPtr f = fold_binop(e->binop, a->value, b->value))
          return f;
      return ir::binop(e->binop, a, b);
    }
  }
  return e;
}

// Replace Reg(kRegC) with the condition value computed in this block.
ir::ExprPtr subst_c(const ir::ExprPtr &e, const ir::ExprPtr &c) {
  if (!e) return e;
  switch (e->kind) {
    case ir::ExprKind::Const: return e;
    case ir::ExprKind::Reg:   return (e->reg == ir::kRegC && c) ? c : e;
    case ir::ExprKind::UnOp:  return ir::unop(e->unop, subst_c(e->a, c));
    case ir::ExprKind::BinOp: return ir::binop(e->binop, subst_c(e->a, c), subst_c(e->b, c));
  }
  return e;
}

// Fold logical-not: !!x -> x, !(a!=b) -> a==b, !(a==b) -> a!=b.
ir::ExprPtr fold_not(const ir::ExprPtr &e) {
  if (!e) return e;
  if (e->kind == ir::ExprKind::UnOp && e->unop == ir::UnOp::LNot) {
    ir::ExprPtr x = fold_not(e->a);
    if (x->kind == ir::ExprKind::UnOp && x->unop == ir::UnOp::LNot)
      return x->a;
    if (x->kind == ir::ExprKind::BinOp && x->binop == ir::BinOp::CmpNe)
      return ir::binop(ir::BinOp::CmpEq, x->a, x->b);
    if (x->kind == ir::ExprKind::BinOp && x->binop == ir::BinOp::CmpEq)
      return ir::binop(ir::BinOp::CmpNe, x->a, x->b);
    return ir::unop(ir::UnOp::LNot, x);
  }
  if (e->kind == ir::ExprKind::UnOp) return ir::unop(e->unop, fold_not(e->a));
  if (e->kind == ir::ExprKind::BinOp) return ir::binop(e->binop, fold_not(e->a), fold_not(e->b));
  return e;
}

void count_reads(const ir::ExprPtr &e, std::map<int, int> &reads) {
  if (!e) return;
  switch (e->kind) {
    case ir::ExprKind::Const: break;
    case ir::ExprKind::Reg: reads[e->reg]++; break;
    case ir::ExprKind::UnOp: count_reads(e->a, reads); break;
    case ir::ExprKind::BinOp: count_reads(e->a, reads); count_reads(e->b, reads); break;
  }
}

} // namespace

void simplify(ir::Function &fn) {
  // Pass 1: per-block constant propagation + condition (C-bit) inlining.
  for (auto &b : fn.blocks) {
    std::map<int, ir::ExprPtr> cprop;  // const-only, intra-block
    ir::ExprPtr c_value;               // last value assigned to the C bit

    for (auto &s : b.stmts) {
      if (s.kind != ir::StmtKind::Assign) { cprop.clear(); continue; }
      s.expr = rewrite(s.expr, cprop);
      if (s.dst_reg == ir::kRegC) {
        c_value = s.expr;
      } else if (is_const(s.expr)) {
        cprop[s.dst_reg] = s.expr;
      } else {
        cprop.erase(s.dst_reg);
      }
    }

    b.term.cond = fold_not(subst_c(rewrite(b.term.cond, cprop), c_value));
    b.term.value = rewrite(b.term.value, cprop);
  }

  // Pass 2: dead-assignment elimination. Remove an assignment whose destination
  // is read nowhere in the (post-substitution) function. Sound without liveness
  // analysis; in particular the inlined C-bit compare becomes unread and drops.
  std::map<int, int> reads;
  for (auto &b : fn.blocks) {
    for (auto &s : b.stmts)
      if (s.kind == ir::StmtKind::Assign) count_reads(s.expr, reads);
    count_reads(b.term.cond, reads);
    count_reads(b.term.value, reads);
  }
  for (auto &b : fn.blocks) {
    std::vector<ir::Stmt> kept;
    kept.reserve(b.stmts.size());
    for (auto &s : b.stmts) {
      if (s.kind == ir::StmtKind::Assign && reads[s.dst_reg] == 0)
        continue;
      kept.push_back(std::move(s));
    }
    b.stmts = std::move(kept);
  }
}

} // namespace opt
