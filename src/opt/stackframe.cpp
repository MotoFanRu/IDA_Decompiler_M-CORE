// Stack-frame recovery: sp-relative memory accesses become named stack-slot
// variables; prologue/epilogue boilerplate is removed. Pure, unit-testable.
#include "opt/opt.h"

#include "ir/ir.h"

#include <map>
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
