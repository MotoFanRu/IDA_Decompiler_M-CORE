#include "emit/emit.h"

#include "ir/ir.h"
#include "vars/vars.h"

#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace emit {

namespace {

struct OpInfo { const char *sym; int prec; };

OpInfo binop_info(ir::BinOp op) {
  switch (op) {
    case ir::BinOp::Mul:   return {"*", 13};
    case ir::BinOp::Add:   return {"+", 12};
    case ir::BinOp::Sub:   return {"-", 12};
    case ir::BinOp::Shl:   return {"<<", 11};
    case ir::BinOp::Shr:   return {">>", 11};
    case ir::BinOp::Sar:   return {">>", 11};
    case ir::BinOp::CmpLt: return {"<", 10};
    case ir::BinOp::CmpHs: return {">=", 10};
    case ir::BinOp::CmpNe: return {"!=", 9};
    case ir::BinOp::CmpEq: return {"==", 9};
    case ir::BinOp::And:   return {"&", 8};
    case ir::BinOp::Xor:   return {"^", 7};
    case ir::BinOp::Or:    return {"|", 6};
  }
  return {"?", 0};
}

constexpr int kUnPrec = 14;

const char *type_name(int size) {
  switch (size) {
    case 1: return "unsigned char";
    case 2: return "unsigned short";
    default: return "int";
  }
}

std::string render(const ir::Expr &e, const vars::VarMap &vm, int min_prec) {
  switch (e.kind) {
    case ir::ExprKind::Const: { std::ostringstream os; os << e.value; return os.str(); }
    case ir::ExprKind::Reg: return vm.name_of(e.reg);
    case ir::ExprKind::UnOp: {
      const char *op = e.unop == ir::UnOp::Neg ? "-" : e.unop == ir::UnOp::Not ? "~" : "!";
      std::string s = std::string(op) + (e.a ? render(*e.a, vm, kUnPrec) : "");
      return kUnPrec < min_prec ? "(" + s + ")" : s;
    }
    case ir::ExprKind::BinOp: {
      OpInfo oi = binop_info(e.binop);
      std::string lhs = e.a ? render(*e.a, vm, oi.prec) : "";
      std::string rhs = e.b ? render(*e.b, vm, oi.prec + 1) : "";
      std::string s = lhs + " " + oi.sym + " " + rhs;
      return oi.prec < min_prec ? "(" + s + ")" : s;
    }
    case ir::ExprKind::Load: {
      std::string s = "*(" + std::string(type_name(e.size)) + " *)(" +
                      (e.a ? render(*e.a, vm, 0) : "") + ")";
      return kUnPrec < min_prec ? "(" + s + ")" : s;
    }
    case ir::ExprKind::Call: {
      std::string callee = !e.name.empty() ? e.name
                           : e.a ? render(*e.a, vm, kUnPrec) : "sub";
      std::string s = callee + "(";
      for (size_t i = 0; i < e.args.size(); ++i) {
        if (i) s += ", ";
        s += render(*e.args[i], vm, 0);
      }
      return s + ")";
    }
  }
  return "?";
}

// One statement as C text (no indent, no newline).
std::string stmt_str(const ir::Stmt &s, const vars::VarMap &vm) {
  switch (s.kind) {
    case ir::StmtKind::Assign:
      return vm.name_of(s.dst_reg) + " = " + (s.expr ? render(*s.expr, vm, 0) : "?") + ";";
    case ir::StmtKind::Store:
      return "*(" + std::string(type_name(s.size)) + " *)(" +
             (s.addr ? render(*s.addr, vm, 0) : "") + ") = " +
             (s.expr ? render(*s.expr, vm, 0) : "?") + ";";
    case ir::StmtKind::Unknown:
      return "// " + s.text;
  }
  return "";
}

ir::ExprPtr negate(const ir::ExprPtr &e) {
  if (e && e->kind == ir::ExprKind::UnOp && e->unop == ir::UnOp::LNot) return e->a;
  if (e && e->kind == ir::ExprKind::BinOp && e->binop == ir::BinOp::CmpNe)
    return ir::binop(ir::BinOp::CmpEq, e->a, e->b);
  if (e && e->kind == ir::ExprKind::BinOp && e->binop == ir::BinOp::CmpEq)
    return ir::binop(ir::BinOp::CmpNe, e->a, e->b);
  return ir::unop(ir::UnOp::LNot, e);
}

std::string ind_s(int n) { return std::string(n * 2, ' '); }

// --------------------------------------------------------------------------

class Structurer {
 public:
  Structurer(const ir::Function &fn, const vars::VarMap &vm) : fn_(fn), vm_(vm) {
    n_ = (int)fn_.blocks.size();
    exit_ = n_;
    for (int i = 0; i < n_; ++i) idx_[fn_.blocks[i].entry] = i;
    build_succ();
    compute_doms();
    compute_postdoms();
    detect_loops();
  }

  bool structurable() const { return structurable_; }

  std::string run() {
    std::ostringstream os;
    emit_seq(0, exit_, 1, os);
    return os.str();
  }

 private:
  struct Loop { std::set<int> body; int exit = 0; int in_succ = 0; ir::ExprPtr stay; };

  int resolve(uint32_t ea) const {
    auto it = idx_.find(ea);
    return it != idx_.end() ? it->second : exit_;
  }

  void build_succ() {
    succ_.assign(n_, {});
    for (int i = 0; i < n_; ++i) {
      const ir::Terminator &t = fn_.blocks[i].term;
      switch (t.kind) {
        case ir::TermKind::Return: succ_[i] = {exit_}; break;
        case ir::TermKind::Goto:
        case ir::TermKind::Fallthrough: succ_[i] = {resolve(t.target)}; break;
        case ir::TermKind::CondBranch:
          succ_[i] = {resolve(t.target), resolve(t.fallthrough)}; break;
      }
    }
  }

  void compute_doms() {
    std::vector<std::vector<int>> preds(n_);
    for (int i = 0; i < n_; ++i)
      for (int s : succ_[i]) if (s != exit_) preds[s].push_back(i);
    std::set<int> all;
    for (int i = 0; i < n_; ++i) all.insert(i);
    dom_.assign(n_, all);
    if (n_ > 0) dom_[0] = {0};
    bool changed = true;
    while (changed) {
      changed = false;
      for (int i = 1; i < n_; ++i) {
        std::set<int> ns;
        bool first = true;
        for (int p : preds[i]) {
          if (first) { ns = dom_[p]; first = false; }
          else {
            std::set<int> tmp;
            for (int x : ns) if (dom_[p].count(x)) tmp.insert(x);
            ns = std::move(tmp);
          }
        }
        ns.insert(i);
        if (ns != dom_[i]) { dom_[i] = std::move(ns); changed = true; }
      }
    }
    preds_ = std::move(preds);
  }

  void compute_postdoms() {
    int m = exit_ + 1;
    pdom_.assign(m, {});
    std::set<int> all;
    for (int i = 0; i < m; ++i) all.insert(i);
    pdom_[exit_] = {exit_};
    for (int i = 0; i < exit_; ++i) pdom_[i] = all;
    bool changed = true;
    while (changed) {
      changed = false;
      for (int i = 0; i < exit_; ++i) {
        std::set<int> inter; bool first = true;
        for (int s : succ_[i]) {
          if (first) { inter = pdom_[s]; first = false; }
          else {
            std::set<int> tmp;
            for (int x : inter) if (pdom_[s].count(x)) tmp.insert(x);
            inter = std::move(tmp);
          }
        }
        inter.insert(i);
        if (inter != pdom_[i]) { pdom_[i] = std::move(inter); changed = true; }
      }
    }
  }

  int ipdom(int i) const {
    std::vector<int> cand;
    for (int c : pdom_[i]) if (c != i) cand.push_back(c);
    for (int c : cand) {
      bool ok = true;
      for (int k : cand) if (k != c && !pdom_[c].count(k)) { ok = false; break; }
      if (ok) return c;
    }
    return exit_;
  }

  void detect_loops() {
    // Back edges: i -> h where h dominates i.
    std::map<int, std::vector<int>> latches;  // header -> latches
    for (int i = 0; i < n_; ++i)
      for (int s : succ_[i])
        if (s != exit_ && dom_[i].count(s)) latches[s].push_back(i);

    for (auto &kv : latches) {
      int h = kv.first;
      Loop lp;
      lp.body.insert(h);
      std::vector<int> stack;
      for (int u : kv.second) if (lp.body.insert(u).second) stack.push_back(u);
      while (!stack.empty()) {
        int u = stack.back(); stack.pop_back();
        for (int p : preds_[u])
          if (lp.body.insert(p).second) stack.push_back(p);
      }

      // Single exit target across the whole loop body.
      int exit_node = -1;
      bool multi_exit = false;
      for (int b : lp.body)
        for (int s : succ_[b])
          if (!lp.body.count(s)) {
            if (exit_node == -1) exit_node = s;
            else if (exit_node != s) multi_exit = true;
          }

      const ir::Terminator &th = fn_.blocks[h].term;
      bool header_ok = th.kind == ir::TermKind::CondBranch &&
                       fn_.blocks[h].stmts.empty();
      if (multi_exit || exit_node == -1 || !header_ok) { structurable_ = false; return; }

      int taken = succ_[h][0], fth = succ_[h][1];
      if (lp.body.count(taken) && !lp.body.count(fth)) {
        lp.in_succ = taken; lp.exit = fth; lp.stay = th.cond;
      } else if (lp.body.count(fth) && !lp.body.count(taken)) {
        lp.in_succ = fth; lp.exit = taken; lp.stay = negate(th.cond);
      } else { structurable_ = false; return; }

      if (lp.exit != exit_node) { structurable_ = false; return; }
      loops_[h] = std::move(lp);
    }

    // No nested loops for now (each header's body must not contain another header).
    for (auto &a : loops_)
      for (auto &b : loops_)
        if (a.first != b.first && a.second.body.count(b.first)) { structurable_ = false; return; }
  }

  void emit_stmts(int bi, int ind, std::ostringstream &os) {
    for (const auto &s : fn_.blocks[bi].stmts)
      os << ind_s(ind) << stmt_str(s, vm_) << "\n";
  }

  void emit_loop(int h, int ind, std::ostringstream &os) {
    const Loop &lp = loops_[h];
    visited_.insert(h);
    os << ind_s(ind) << "while (" << render(*lp.stay, vm_, 0) << ") {\n";
    loopctx_.push_back({h, lp.exit});
    emit_seq(lp.in_succ, h, ind + 1, os);
    loopctx_.pop_back();
    os << ind_s(ind) << "}\n";
  }

  void emit_seq(int n, int stop, int ind, std::ostringstream &os) {
    while (n != stop && n != exit_ && !visited_.count(n)) {
      if (loops_.count(n) && (loopctx_.empty() || loopctx_.back().first != n)) {
        emit_loop(n, ind, os);
        n = loops_[n].exit;
        continue;
      }
      visited_.insert(n);
      emit_stmts(n, ind, os);
      const ir::Terminator &t = fn_.blocks[n].term;

      if (t.kind == ir::TermKind::Return) {
        if (t.has_value && t.value)
          os << ind_s(ind) << "return " << render(*t.value, vm_, 0) << ";\n";
        else
          os << ind_s(ind) << "return;\n";
        return;
      }

      if (t.kind == ir::TermKind::Goto || t.kind == ir::TermKind::Fallthrough) {
        int tn = succ_[n][0];
        if (!loopctx_.empty()) {
          if (tn == loopctx_.back().second) { os << ind_s(ind) << "break;\n"; return; }
          if (tn == loopctx_.back().first && tn != stop) { os << ind_s(ind) << "continue;\n"; return; }
        }
        n = tn;
        continue;
      }

      // CondBranch
      int taken = succ_[n][0], fth = succ_[n][1];
      const ir::ExprPtr &cond = t.cond;

      if (!loopctx_.empty()) {
        int H = loopctx_.back().first, E = loopctx_.back().second;
        if (taken == E) { os << ind_s(ind) << "if (" << render(*cond, vm_, 0) << ") break;\n"; n = fth; continue; }
        if (fth == E)   { os << ind_s(ind) << "if (" << render(*negate(cond), vm_, 0) << ") break;\n"; n = taken; continue; }
        if (taken == H) { os << ind_s(ind) << "if (" << render(*cond, vm_, 0) << ") continue;\n"; n = fth; continue; }
        if (fth == H)   { os << ind_s(ind) << "if (" << render(*negate(cond), vm_, 0) << ") continue;\n"; n = taken; continue; }
      }

      int merge = ipdom(n);
      if (fth == merge) {
        os << ind_s(ind) << "if (" << render(*cond, vm_, 0) << ") {\n";
        emit_seq(taken, merge, ind + 1, os);
        os << ind_s(ind) << "}\n";
      } else if (taken == merge) {
        os << ind_s(ind) << "if (" << render(*negate(cond), vm_, 0) << ") {\n";
        emit_seq(fth, merge, ind + 1, os);
        os << ind_s(ind) << "}\n";
      } else {
        os << ind_s(ind) << "if (" << render(*cond, vm_, 0) << ") {\n";
        emit_seq(taken, merge, ind + 1, os);
        os << ind_s(ind) << "} else {\n";
        emit_seq(fth, merge, ind + 1, os);
        os << ind_s(ind) << "}\n";
      }
      n = merge;
    }
  }

  const ir::Function &fn_;
  const vars::VarMap &vm_;
  int n_ = 0, exit_ = 0;
  std::map<uint32_t, int> idx_;
  std::vector<std::vector<int>> succ_, preds_;
  std::vector<std::set<int>> dom_, pdom_;
  std::map<int, Loop> loops_;
  std::set<int> visited_;
  std::vector<std::pair<int, int>> loopctx_;  // (header, exit)
  bool structurable_ = true;
};

// Goto-based fallback (always correct).
std::string emit_goto(const ir::Function &fn, const vars::VarMap &vm) {
  std::ostringstream os;
  std::set<uint32_t> targets;
  for (const auto &b : fn.blocks) {
    const ir::Terminator &t = b.term;
    if (t.kind == ir::TermKind::Goto || t.kind == ir::TermKind::Fallthrough) targets.insert(t.target);
    if (t.kind == ir::TermKind::CondBranch) { targets.insert(t.target); targets.insert(t.fallthrough); }
  }
  auto label = [](uint32_t ea) { std::ostringstream l; l << "loc_" << std::hex << ea; return l.str(); };
  for (size_t i = 0; i < fn.blocks.size(); ++i) {
    const ir::Block &b = fn.blocks[i];
    uint32_t next = i + 1 < fn.blocks.size() ? fn.blocks[i + 1].entry : 0xffffffff;
    if (targets.count(b.entry)) os << label(b.entry) << ":\n";
    for (const auto &s : b.stmts)
      os << "  " << stmt_str(s, vm) << "\n";
    const ir::Terminator &t = b.term;
    switch (t.kind) {
      case ir::TermKind::Return:
        os << (t.has_value && t.value ? "  return " + render(*t.value, vm, 0) + ";\n" : "  return;\n");
        break;
      case ir::TermKind::Goto:
      case ir::TermKind::Fallthrough:
        if (t.target != next) os << "  goto " << label(t.target) << ";\n";
        break;
      case ir::TermKind::CondBranch:
        os << "  if (" << render(*t.cond, vm, 0) << ") goto " << label(t.target) << ";\n";
        if (t.fallthrough != next) os << "  goto " << label(t.fallthrough) << ";\n";
        break;
    }
  }
  return os.str();
}

} // namespace

std::string emit_expr(const ir::Expr &e, const vars::VarMap &vm) { return render(e, vm, 0); }

std::string emit_c(const ir::Function &fn, const vars::VarMap &vm) {
  std::ostringstream os;
  const std::string name = fn.name.empty() ? "sub" : fn.name;
  os << "int " << name << "(";
  if (vm.params.empty()) {
    os << "void";
  } else {
    for (size_t i = 0; i < vm.params.size(); ++i) {
      if (i) os << ", ";
      os << "int " << vm.name_of(vm.params[i]);
    }
  }
  os << ")\n{\n";
  if (!fn.blocks.empty()) {
    Structurer st(fn, vm);
    os << (st.structurable() ? st.run() : emit_goto(fn, vm));
  }
  os << "}\n";
  return os.str();
}

} // namespace emit
