#include "opt/opt.h"

#include "ir/ir.h"

#include <algorithm>
#include <map>
#include <set>
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
    case ir::ExprKind::Cast: {
      ir::ExprPtr a = rewrite(e->a, defs);
      if (is_const(a)) {
        int64_t v = a->value;
        switch (e->size) {
          case 1: return ir::constant(e->is_signed ? (int8_t)v : (uint8_t)v);
          case 2: return ir::constant(e->is_signed ? (int16_t)v : (uint16_t)v);
          default: return ir::constant(e->is_signed ? (int32_t)v : (uint32_t)v);
        }
      }
      return ir::cast(e->size, e->is_signed, a);
    }
    case ir::ExprKind::Load:
      return ir::load(rewrite(e->a, defs), e->size);
    case ir::ExprKind::Call: {
      std::vector<ir::ExprPtr> args;
      for (const auto &x : e->args) args.push_back(rewrite(x, defs));
      return e->a ? ir::call_indirect(rewrite(e->a, defs), std::move(args))
                  : ir::call(e->name, std::move(args));
    }
  }
  return e;
}

bool has_call(const ir::ExprPtr &e) {
  if (!e) return false;
  if (e->kind == ir::ExprKind::Call) return true;
  if (e->kind == ir::ExprKind::Load || e->kind == ir::ExprKind::UnOp ||
      e->kind == ir::ExprKind::Cast)
    return has_call(e->a);
  if (e->kind == ir::ExprKind::BinOp) return has_call(e->a) || has_call(e->b);
  return false;
}

// Replace Reg(kRegC) with the condition value computed in this block.
ir::ExprPtr subst_c(const ir::ExprPtr &e, const ir::ExprPtr &c) {
  if (!e) return e;
  switch (e->kind) {
    case ir::ExprKind::Const: return e;
    case ir::ExprKind::Reg:   return (e->reg == ir::kRegC && c) ? c : e;
    case ir::ExprKind::UnOp:  return ir::unop(e->unop, subst_c(e->a, c));
    case ir::ExprKind::BinOp: return ir::binop(e->binop, subst_c(e->a, c), subst_c(e->b, c));
    case ir::ExprKind::Load:  return ir::load(subst_c(e->a, c), e->size);
    case ir::ExprKind::Cast:  return ir::cast(e->size, e->is_signed, subst_c(e->a, c));
    case ir::ExprKind::Call:  return e;  // C-bit never flows into call args here
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
    case ir::ExprKind::UnOp:
    case ir::ExprKind::Cast:
    case ir::ExprKind::Load: count_reads(e->a, reads); break;
    case ir::ExprKind::BinOp: count_reads(e->a, reads); count_reads(e->b, reads); break;
    case ir::ExprKind::Call:
      count_reads(e->a, reads);
      for (const auto &x : e->args) count_reads(x, reads);
      break;
  }
}

bool expr_equal(const ir::ExprPtr &a, const ir::ExprPtr &b) {
  if (!a || !b) return a == b;
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case ir::ExprKind::Const: return a->value == b->value && a->name == b->name;
    case ir::ExprKind::Reg: return a->reg == b->reg;
    case ir::ExprKind::UnOp: return a->unop == b->unop && expr_equal(a->a, b->a);
    case ir::ExprKind::Cast:
      return a->size == b->size && a->is_signed == b->is_signed && expr_equal(a->a, b->a);
    case ir::ExprKind::Load: return a->size == b->size && expr_equal(a->a, b->a);
    case ir::ExprKind::BinOp:
      return a->binop == b->binop && expr_equal(a->a, b->a) && expr_equal(a->b, b->b);
    case ir::ExprKind::Call: return false;  // treat calls as never equal
  }
  return false;
}

// Reaching definition of the single C bit at each block's entry (nullptr = none
// or conflicting). Lets a compare in one block resolve a branch/mvc in another.
std::vector<ir::ExprPtr> compute_c_reaching(const ir::Function &fn) {
  int n = (int)fn.blocks.size();
  std::map<uint32_t, int> idx;
  for (int i = 0; i < n; ++i) idx[fn.blocks[i].entry] = i;

  std::vector<std::vector<int>> preds(n);
  auto succs = [&](int i) {
    std::vector<int> out;
    const ir::Terminator &t = fn.blocks[i].term;
    auto add = [&](uint32_t ea) { auto it = idx.find(ea); if (it != idx.end()) out.push_back(it->second); };
    if (t.kind == ir::TermKind::Goto || t.kind == ir::TermKind::Fallthrough) add(t.target);
    if (t.kind == ir::TermKind::CondBranch) { add(t.target); add(t.fallthrough); }
    return out;
  };
  for (int i = 0; i < n; ++i)
    for (int s : succs(i)) preds[s].push_back(i);

  // Per-block: mode 0 = pass-through, 1 = defines C (def), 2 = kills C.
  std::vector<int> mode(n, 0);
  std::vector<ir::ExprPtr> def(n);
  for (int i = 0; i < n; ++i) {
    int m = 0; ir::ExprPtr d;
    for (const auto &s : fn.blocks[i].stmts) {
      if (s.kind == ir::StmtKind::Assign && s.dst_reg == ir::kRegC) { m = 1; d = s.expr; }
      else if (s.kind == ir::StmtKind::Unknown) { m = 2; d = nullptr; }
      else if (s.kind == ir::StmtKind::Assign && has_call(s.expr)) { m = 2; d = nullptr; }
    }
    mode[i] = m; def[i] = d;
  }

  std::vector<ir::ExprPtr> c_in(n), c_out(n);
  for (int iter = 0; iter < n + 2; ++iter) {
    for (int i = 0; i < n; ++i)
      c_out[i] = mode[i] == 1 ? def[i] : mode[i] == 2 ? nullptr : c_in[i];
    bool changed = false;
    for (int i = 0; i < n; ++i) {
      ir::ExprPtr meet; bool first = true, ok = true;
      for (int p : preds[i]) {
        if (first) { meet = c_out[p]; first = false; }
        else if (!expr_equal(meet, c_out[p])) { ok = false; }
      }
      ir::ExprPtr nv = (first || !ok) ? nullptr : meet;
      if (!expr_equal(nv, c_in[i])) { c_in[i] = nv; changed = true; }
    }
    if (!changed) break;
  }
  return c_in;
}

} // namespace

void simplify(ir::Function &fn) {
  std::vector<ir::ExprPtr> c_in = compute_c_reaching(fn);

  // Pass 1: per-block constant propagation + condition (C-bit) inlining. The C
  // value (`c_cur`) starts from the cross-block reaching definition and is
  // substituted into every C read (branches, mvc/mvcv), so the synthetic C
  // register never reaches the output.
  for (size_t bi = 0; bi < fn.blocks.size(); ++bi) {
    ir::Block &b = fn.blocks[bi];
    std::map<int, ir::ExprPtr> cprop;  // const-only, intra-block
    ir::ExprPtr c_cur = c_in[bi];

    auto resolve = [&](const ir::ExprPtr &e) {
      return fold_not(subst_c(rewrite(e, cprop), c_cur));
    };

    for (auto &s : b.stmts) {
      if (s.kind == ir::StmtKind::Store) {
        s.addr = rewrite(s.addr, cprop);
        s.expr = resolve(s.expr);
        continue;
      }
      if (s.kind != ir::StmtKind::Assign) { cprop.clear(); c_cur = nullptr; continue; }
      s.expr = resolve(s.expr);
      if (has_call(s.expr)) { cprop.clear(); c_cur = nullptr; }  // call clobbers regs + C
      if (s.dst_reg == ir::kRegC) {
        c_cur = s.expr;
      } else if (is_const(s.expr)) {
        cprop[s.dst_reg] = s.expr;
      } else {
        cprop.erase(s.dst_reg);
      }
    }

    b.term.cond = resolve(b.term.cond);
    b.term.value = resolve(b.term.value);
  }

  // Pass 2: dead-assignment elimination. Remove an assignment whose destination
  // is read nowhere in the (post-substitution) function. Sound without liveness
  // analysis; in particular the inlined C-bit compare becomes unread and drops.
  std::map<int, int> reads;
  for (auto &b : fn.blocks) {
    for (auto &s : b.stmts) {
      count_reads(s.expr, reads);   // Assign rhs / Store value
      count_reads(s.addr, reads);   // Store address
    }
    count_reads(b.term.cond, reads);
    count_reads(b.term.value, reads);
  }
  for (auto &b : fn.blocks) {
    std::vector<ir::Stmt> kept;
    kept.reserve(b.stmts.size());
    for (auto &s : b.stmts) {
      // Drop only dead register assignments; keep stores and calls (side effects).
      if (s.kind == ir::StmtKind::Assign && reads[s.dst_reg] == 0 && !has_call(s.expr))
        continue;
      kept.push_back(std::move(s));
    }
    b.stmts = std::move(kept);
  }
}

namespace {

bool uses_reg(const ir::ExprPtr &e, int reg) {
  if (!e) return false;
  if (e->kind == ir::ExprKind::Reg) return e->reg == reg;
  if (uses_reg(e->a, reg) || uses_reg(e->b, reg)) return true;
  for (const auto &x : e->args) if (uses_reg(x, reg)) return true;
  return false;
}

void reads_set(const ir::ExprPtr &e, std::set<int> &s) {
  if (!e) return;
  if (e->kind == ir::ExprKind::Reg) s.insert(e->reg);
  reads_set(e->a, s);
  reads_set(e->b, s);
  for (const auto &x : e->args) reads_set(x, s);
}

// May a definition with `usecount` total uses be substituted into its uses?
bool propagatable(const ir::ExprPtr &e, int usecount) {
  if (!e) return false;
  switch (e->kind) {
    case ir::ExprKind::Const:
    case ir::ExprKind::Reg: return true;            // copy/const: any number of uses
    case ir::ExprKind::Cast: return propagatable(e->a, usecount);
    case ir::ExprKind::UnOp:
    case ir::ExprKind::BinOp: return usecount == 1; // pure arithmetic: single use only
    case ir::ExprKind::Load:
    case ir::ExprKind::Call: return false;          // side effects / ordering
  }
  return false;
}

ir::ExprPtr subst_inline(const ir::ExprPtr &e, const std::map<int, ir::ExprPtr> &defs) {
  if (!e) return e;
  switch (e->kind) {
    case ir::ExprKind::Reg: {
      auto it = defs.find(e->reg);
      return it != defs.end() ? it->second : e;
    }
    case ir::ExprKind::BinOp: return ir::binop(e->binop, subst_inline(e->a, defs), subst_inline(e->b, defs));
    case ir::ExprKind::UnOp:  return ir::unop(e->unop, subst_inline(e->a, defs));
    case ir::ExprKind::Cast:  return ir::cast(e->size, e->is_signed, subst_inline(e->a, defs));
    case ir::ExprKind::Load:  return ir::load(subst_inline(e->a, defs), e->size);
    case ir::ExprKind::Call: {
      std::vector<ir::ExprPtr> args;
      for (const auto &x : e->args) args.push_back(subst_inline(x, defs));
      return e->a ? ir::call_indirect(subst_inline(e->a, defs), std::move(args))
                  : ir::call(e->name, std::move(args));
    }
    default: return e;
  }
}

} // namespace

void inline_locals(ir::Function &fn) {
  // Total use counts (reads) per register across the function.
  std::map<int, int> uses;
  for (auto &b : fn.blocks) {
    for (auto &s : b.stmts) { count_reads(s.expr, uses); count_reads(s.addr, uses); }
    count_reads(b.term.cond, uses);
    count_reads(b.term.value, uses);
  }

  // Forward intra-block propagation / single-use inlining.
  for (auto &b : fn.blocks) {
    std::map<int, ir::ExprPtr> defs;
    for (auto &s : b.stmts) {
      if (s.kind == ir::StmtKind::Store) {
        s.addr = subst_inline(s.addr, defs);
        s.expr = subst_inline(s.expr, defs);
        continue;
      }
      if (s.kind != ir::StmtKind::Assign) { defs.clear(); continue; }
      s.expr = subst_inline(s.expr, defs);
      // A redefinition invalidates defs that referenced the old value.
      for (auto it = defs.begin(); it != defs.end();)
        it = (it->first == s.dst_reg || uses_reg(it->second, s.dst_reg)) ? defs.erase(it) : ++it;
      if (s.dst_reg != ir::kRegC && propagatable(s.expr, uses[s.dst_reg]))
        defs[s.dst_reg] = s.expr;
    }
    b.term.cond = subst_inline(b.term.cond, defs);
    b.term.value = subst_inline(b.term.value, defs);
  }

  // Liveness-based dead-store elimination. live-out of a block over-approximates
  // as registers read by any other block's statements/terminator.
  int n = (int)fn.blocks.size();
  std::vector<std::set<int>> reads_in(n);
  for (int i = 0; i < n; ++i) {
    for (auto &s : fn.blocks[i].stmts) { reads_set(s.expr, reads_in[i]); reads_set(s.addr, reads_in[i]); }
    reads_set(fn.blocks[i].term.cond, reads_in[i]);
    reads_set(fn.blocks[i].term.value, reads_in[i]);
  }
  for (int i = 0; i < n; ++i) {
    std::set<int> live;
    for (int j = 0; j < n; ++j) if (j != i) live.insert(reads_in[j].begin(), reads_in[j].end());
    reads_set(fn.blocks[i].term.cond, live);
    reads_set(fn.blocks[i].term.value, live);

    std::vector<ir::Stmt> rev;
    auto &stmts = fn.blocks[i].stmts;
    for (auto it = stmts.rbegin(); it != stmts.rend(); ++it) {
      ir::Stmt &s = *it;
      if (s.kind == ir::StmtKind::Assign && s.dst_reg != ir::kRegC && !has_call(s.expr) &&
          live.find(s.dst_reg) == live.end()) {
        continue;  // dead store
      }
      if (s.kind == ir::StmtKind::Assign) live.erase(s.dst_reg);
      reads_set(s.expr, live);
      reads_set(s.addr, live);
      rev.push_back(std::move(s));
    }
    std::reverse(rev.begin(), rev.end());
    stmts = std::move(rev);
  }
}

} // namespace opt
