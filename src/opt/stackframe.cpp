// Stack-frame recovery: sp-relative memory accesses become named stack-slot
// variables; prologue/epilogue boilerplate is removed. Pure, unit-testable.
#include "opt/opt.h"

#include "ir/ir.h"

#include <array>
#include <map>
#include <set>
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

// Replace sp-relative loads with stack-slot register references, and sp-relative
// address values (sp / sp + off used as a pointer) with &var_off.
ir::ExprPtr rw(const ir::ExprPtr &e) {
  if (!e) return e;
  switch (e->kind) {
    case ir::ExprKind::Reg:
      if (e->reg == ir::kRegSP)  // bare sp used as a value -> &var_0
        return ir::unop(ir::UnOp::AddrOf, ir::reg(ir::kStackBase));
      return e;
    case ir::ExprKind::Load: {
      int64_t off;
      if (sp_offset(e->a, off)) return ir::reg(ir::kStackBase + (int)off);
      return ir::load(rw(e->a), e->size);
    }
    case ir::ExprKind::BinOp: {
      int64_t off;
      if (e->binop == ir::BinOp::Add && sp_offset(e, off))  // (sp + off) value -> &var_off
        return ir::unop(ir::UnOp::AddrOf, ir::reg(ir::kStackBase + (int)off));
      return ir::binop(e->binop, rw(e->a), rw(e->b));
    }
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

namespace {

bool is_call_expr(const ir::ExprPtr &e) { return e && e->kind == ir::ExprKind::Call; }

// Mark argument registers (r2..r7) read by `e`.
void collect_arg_reads(const ir::ExprPtr &e, std::array<bool, 8> &rd) {
  if (!e) return;
  switch (e->kind) {
    case ir::ExprKind::Reg:
      if (e->reg >= 2 && e->reg <= 7) rd[e->reg] = true;
      return;
    case ir::ExprKind::BinOp:
      collect_arg_reads(e->a, rd); collect_arg_reads(e->b, rd); return;
    case ir::ExprKind::UnOp:
    case ir::ExprKind::Cast:
    case ir::ExprKind::Load:
      collect_arg_reads(e->a, rd); return;
    case ir::ExprKind::Select:
      collect_arg_reads(e->a, rd); collect_arg_reads(e->b, rd);
      if (!e->args.empty()) collect_arg_reads(e->args[0], rd);
      return;
    case ir::ExprKind::Call:
      collect_arg_reads(e->a, rd);
      for (const auto &x : e->args) collect_arg_reads(x, rd);
      return;
    default:
      return;
  }
}

// Argument registers (r2..r7) available at a point, scanning backward. A call
// clobbers the caller-saved arg registers, so it acts as a barrier; its own
// result is claimed only if read since (live and threaded on, as in
// `p = alloc(); ...; memset(p, ..)`). At a control-flow merge the analysis
// recurses into every predecessor and intersects — a register counts only if it
// is provided on all incoming paths. `read_seen` (by value) tracks registers
// read on the continuation; `target_reg` (the callee pointer of an indirect
// call) is excluded. `budget` bounds total work across branching paths.
std::array<bool, 8> avail_args(const ir::Function &fn,
                               const std::vector<std::vector<int>> &preds, int blk,
                               int from_init, bool first_block,
                               std::array<bool, 8> read_seen, int target_reg,
                               std::set<int> visited, int &budget) {
  std::array<bool, 8> defined{};
  if (budget <= 0 || visited.count(blk)) return defined;
  --budget;
  visited.insert(blk);
  const auto &b = fn.blocks[blk];
  int from = first_block ? from_init : (int)b.stmts.size() - 1;
  for (int i = from; i >= 0; --i) {
    const ir::Stmt &s = b.stmts[i];
    if (s.kind == ir::StmtKind::Assign && is_call_expr(s.expr)) {
      if (s.dst_reg >= 2 && s.dst_reg <= 7 && s.dst_reg != target_reg &&
          read_seen[s.dst_reg])
        defined[s.dst_reg] = true;
      return defined;  // barrier: caller-saved regs before it are clobbered
    }
    if (s.kind == ir::StmtKind::Assign && s.dst_reg >= 2 && s.dst_reg <= 7 &&
        s.dst_reg != target_reg)
      defined[s.dst_reg] = true;
    collect_arg_reads(s.expr, read_seen);
    collect_arg_reads(s.addr, read_seen);
  }
  // Reached the top of the block with no barrier: pull in from predecessors.
  const auto &ps = preds[blk];
  std::array<bool, 8> from_preds{};
  if (ps.size() == 1) {
    from_preds = avail_args(fn, preds, ps[0], 0, false, read_seen, target_reg, visited, budget);
  } else if (ps.size() > 1) {
    bool firstp = true;
    for (int p : ps) {
      auto pr = avail_args(fn, preds, p, 0, false, read_seen, target_reg, visited, budget);
      if (firstp) { from_preds = pr; firstp = false; }
      else for (int r = 2; r <= 7; ++r) from_preds[r] = from_preds[r] && pr[r];  // must hold on all paths
    }
  }
  for (int r = 2; r <= 7; ++r) defined[r] = defined[r] || from_preds[r];
  return defined;
}

// Arguments of the call at (blk, stmt_i): the contiguous r2.. run of registers
// available just before it.
std::vector<ir::ExprPtr> args_for_call(const ir::Function &fn,
                                       const std::vector<std::vector<int>> &preds,
                                       int blk, int stmt_i, int target_reg) {
  int budget = 400;
  std::array<bool, 8> defined =
      avail_args(fn, preds, blk, stmt_i - 1, true, {}, target_reg, {}, budget);
  std::vector<ir::ExprPtr> args;
  for (int r = 2; r <= 7 && defined[r]; ++r) args.push_back(ir::reg(r));
  return args;
}

}  // namespace

void recover_call_args(ir::Function &fn) {
  int n = (int)fn.blocks.size();
  std::map<uint32_t, int> idx;
  for (int i = 0; i < n; ++i) idx[fn.blocks[i].entry] = i;

  std::vector<std::vector<int>> preds(n);
  for (int i = 0; i < n; ++i) {
    const ir::Terminator &t = fn.blocks[i].term;
    auto add = [&](uint32_t ea) {
      auto it = idx.find(ea);
      if (it != idx.end()) preds[it->second].push_back(i);
    };
    if (t.kind == ir::TermKind::Goto || t.kind == ir::TermKind::Fallthrough) add(t.target);
    if (t.kind == ir::TermKind::CondBranch) { add(t.target); add(t.fallthrough); }
  }

  auto target_of = [](const ir::ExprPtr &call) {
    return call->a && call->a->kind == ir::ExprKind::Reg ? call->a->reg : -1;
  };
  for (int bi = 0; bi < n; ++bi) {
    auto &b = fn.blocks[bi];
    for (int i = 0; i < (int)b.stmts.size(); ++i) {
      ir::Stmt &s = b.stmts[i];
      if (s.kind == ir::StmtKind::Assign && is_call_expr(s.expr))
        s.expr->args = args_for_call(fn, preds, bi, i, target_of(s.expr));
    }
    if (b.term.value && is_call_expr(b.term.value))  // tail call
      b.term.value->args =
          args_for_call(fn, preds, bi, (int)b.stmts.size(), target_of(b.term.value));
  }
}

namespace {

// Replace each register read with its current versioned id.
ir::ExprPtr rename_reads(const ir::ExprPtr &e, const std::map<int, int> &ver) {
  if (!e) return e;
  switch (e->kind) {
    case ir::ExprKind::Reg: {
      auto it = ver.find(e->reg);
      return it != ver.end() ? ir::reg(it->second) : e;
    }
    case ir::ExprKind::UnOp: return ir::unop(e->unop, rename_reads(e->a, ver));
    case ir::ExprKind::Cast: return ir::cast(e->size, e->is_signed, rename_reads(e->a, ver));
    case ir::ExprKind::Load: return ir::load(rename_reads(e->a, ver), e->size);
    case ir::ExprKind::BinOp:
      return ir::binop(e->binop, rename_reads(e->a, ver), rename_reads(e->b, ver));
    case ir::ExprKind::Select:
      return ir::select(rename_reads(e->a, ver), rename_reads(e->b, ver),
                        rename_reads(e->args.empty() ? nullptr : e->args[0], ver));
    case ir::ExprKind::Call: {
      std::vector<ir::ExprPtr> args;
      for (const auto &x : e->args) args.push_back(rename_reads(x, ver));
      return e->a ? ir::call_indirect(rename_reads(e->a, ver), std::move(args))
                  : ir::call(e->name, std::move(args));
    }
    default: return e;
  }
}

bool is_gp(int r) { return r >= 0 && r <= 15; }

} // namespace

void split_ranges(ir::Function &fn) {
  int ctr = 0;
  for (auto &b : fn.blocks) {
    // Index of each GP register's last definition in the block.
    std::map<int, int> last_def;
    for (size_t i = 0; i < b.stmts.size(); ++i)
      if (b.stmts[i].kind == ir::StmtKind::Assign && is_gp(b.stmts[i].dst_reg))
        last_def[b.stmts[i].dst_reg] = (int)i;

    std::map<int, int> ver;  // GP reg -> current versioned id (absent = original)
    for (size_t i = 0; i < b.stmts.size(); ++i) {
      ir::Stmt &s = b.stmts[i];
      s.addr = rename_reads(s.addr, ver);
      s.expr = rename_reads(s.expr, ver);
      if (s.kind == ir::StmtKind::Unknown) { ver.clear(); continue; }
      if (s.kind != ir::StmtKind::Assign) continue;
      int d = s.dst_reg;
      if (is_gp(d) && last_def[d] != (int)i) {
        int t = ir::kRenameBase + ctr++;  // earlier value -> its own variable
        ver[d] = t;
        s.dst_reg = t;
      } else {
        ver.erase(d);  // last def of d (or non-GP): keep the original id
      }
    }
    b.term.cond = rename_reads(b.term.cond, ver);
    b.term.value = rename_reads(b.term.value, ver);
  }
}

} // namespace opt
