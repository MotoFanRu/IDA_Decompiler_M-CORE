#include "emit/emit.h"

#include "ir/ir.h"
#include "vars/vars.h"

#include <functional>
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

std::string render(const ir::Expr &e, const vars::VarMap &vm, int min_prec) {
  switch (e.kind) {
    case ir::ExprKind::Const: {
      std::ostringstream os; os << e.value; return os.str();
    }
    case ir::ExprKind::Reg:
      return vm.name_of(e.reg);
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
  }
  return "?";
}

// Logical negation, folding double-not and (in)equality.
ir::ExprPtr negate(const ir::ExprPtr &e) {
  if (e && e->kind == ir::ExprKind::UnOp && e->unop == ir::UnOp::LNot)
    return e->a;
  if (e && e->kind == ir::ExprKind::BinOp && e->binop == ir::BinOp::CmpNe)
    return ir::binop(ir::BinOp::CmpEq, e->a, e->b);
  if (e && e->kind == ir::ExprKind::BinOp && e->binop == ir::BinOp::CmpEq)
    return ir::binop(ir::BinOp::CmpNe, e->a, e->b);
  return ir::unop(ir::UnOp::LNot, e);
}

std::string indent_str(int n) { return std::string(n * 2, ' '); }

// --- Structured emission over a (reducible, loop-free) CFG -------------------

class Structurer {
 public:
  Structurer(const ir::Function &fn, const vars::VarMap &vm) : fn_(fn), vm_(vm) {
    for (size_t i = 0; i < fn_.blocks.size(); ++i)
      idx_[fn_.blocks[i].entry] = (int)i;
    exit_ = (int)fn_.blocks.size();
    compute_postdoms();
  }

  std::string run() {
    std::ostringstream os;
    emit_region(0, exit_, 1, os);
    return os.str();
  }

  // True if the CFG has no back edges (DAG): structured emission is valid.
  bool is_dag() const {
    std::vector<int> state(fn_.blocks.size(), 0);  // 0=unseen,1=onstack,2=done
    std::function<bool(int)> dfs = [&](int n) {
      if (n == exit_) return true;
      state[n] = 1;
      for (int s : succ(n)) {
        if (s == exit_) continue;
        if (state[s] == 1) return false;          // back edge
        if (state[s] == 0 && !dfs(s)) return false;
      }
      state[n] = 2;
      return true;
    };
    return fn_.blocks.empty() ? true : dfs(0);
  }

 private:
  std::vector<int> succ(int i) const {
    if (i == exit_) return {};
    const ir::Terminator &t = fn_.blocks[i].term;
    auto resolve = [&](uint32_t ea) {
      auto it = idx_.find(ea);
      return it != idx_.end() ? it->second : exit_;
    };
    switch (t.kind) {
      case ir::TermKind::Return: return {exit_};
      case ir::TermKind::Goto:
      case ir::TermKind::Fallthrough: return {resolve(t.target)};
      case ir::TermKind::CondBranch: return {resolve(t.target), resolve(t.fallthrough)};
    }
    return {exit_};
  }

  void compute_postdoms() {
    int n = exit_ + 1;
    pdom_.assign(n, {});
    std::set<int> all;
    for (int i = 0; i < n; ++i) all.insert(i);
    pdom_[exit_] = {exit_};
    for (int i = 0; i < exit_; ++i) pdom_[i] = all;
    bool changed = true;
    while (changed) {
      changed = false;
      for (int i = 0; i < exit_; ++i) {
        std::set<int> inter;
        bool first = true;
        for (int s : succ(i)) {
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
    // Closest post-dominator: the candidate c (post-dom of i, != i) that is
    // post-dominated by every other candidate.
    std::vector<int> cand;
    for (int c : pdom_[i]) if (c != i) cand.push_back(c);
    for (int c : cand) {
      bool ok = true;
      for (int k : cand)
        if (k != c && !pdom_[c].count(k)) { ok = false; break; }
      if (ok) return c;
    }
    return exit_;
  }

  void emit_stmts(int bi, int ind, std::ostringstream &os) {
    for (const auto &s : fn_.blocks[bi].stmts) {
      if (s.kind == ir::StmtKind::Assign)
        os << indent_str(ind) << vm_.name_of(s.dst_reg) << " = "
           << (s.expr ? render(*s.expr, vm_, 0) : "?") << ";\n";
      else
        os << indent_str(ind) << "// " << s.text << "\n";
    }
  }

  void emit_region(int bi, int stop, int ind, std::ostringstream &os) {
    int n = bi;
    while (n != stop && n != exit_ && !visited_.count(n)) {
      visited_.insert(n);
      emit_stmts(n, ind, os);
      const ir::Terminator &t = fn_.blocks[n].term;

      if (t.kind == ir::TermKind::Return) {
        if (t.has_value && t.value)
          os << indent_str(ind) << "return " << render(*t.value, vm_, 0) << ";\n";
        else
          os << indent_str(ind) << "return;\n";
        return;
      }
      if (t.kind == ir::TermKind::Goto || t.kind == ir::TermKind::Fallthrough) {
        auto it = idx_.find(t.target);
        n = it != idx_.end() ? it->second : exit_;
        continue;
      }
      // CondBranch
      int taken = succ(n)[0], fth = succ(n)[1];
      int merge = ipdom(n);
      std::string cond = render(*t.cond, vm_, 0);
      if (fth == merge) {
        os << indent_str(ind) << "if (" << cond << ") {\n";
        emit_region(taken, merge, ind + 1, os);
        os << indent_str(ind) << "}\n";
      } else if (taken == merge) {
        os << indent_str(ind) << "if (" << render(*negate(t.cond), vm_, 0) << ") {\n";
        emit_region(fth, merge, ind + 1, os);
        os << indent_str(ind) << "}\n";
      } else {
        os << indent_str(ind) << "if (" << cond << ") {\n";
        emit_region(taken, merge, ind + 1, os);
        os << indent_str(ind) << "} else {\n";
        emit_region(fth, merge, ind + 1, os);
        os << indent_str(ind) << "}\n";
      }
      n = merge;
    }
  }

  const ir::Function &fn_;
  const vars::VarMap &vm_;
  std::map<uint32_t, int> idx_;
  std::vector<std::set<int>> pdom_;
  std::set<int> visited_;
  int exit_ = 0;
};

// --- Goto-based fallback (always correct, used when the CFG has loops) -------

std::string emit_goto(const ir::Function &fn, const vars::VarMap &vm) {
  std::ostringstream os;
  std::set<uint32_t> targets;
  for (const auto &b : fn.blocks) {
    const ir::Terminator &t = b.term;
    if (t.kind == ir::TermKind::Goto || t.kind == ir::TermKind::Fallthrough)
      targets.insert(t.target);
    if (t.kind == ir::TermKind::CondBranch) {
      targets.insert(t.target);
      targets.insert(t.fallthrough);
    }
  }
  auto label = [](uint32_t ea) {
    std::ostringstream l; l << "loc_" << std::hex << ea; return l.str();
  };
  for (size_t i = 0; i < fn.blocks.size(); ++i) {
    const ir::Block &b = fn.blocks[i];
    uint32_t next = i + 1 < fn.blocks.size() ? fn.blocks[i + 1].entry : 0xffffffff;
    if (targets.count(b.entry)) os << label(b.entry) << ":\n";
    for (const auto &s : b.stmts) {
      if (s.kind == ir::StmtKind::Assign)
        os << "  " << vm.name_of(s.dst_reg) << " = "
           << (s.expr ? render(*s.expr, vm, 0) : "?") << ";\n";
      else
        os << "  // " << s.text << "\n";
    }
    const ir::Terminator &t = b.term;
    switch (t.kind) {
      case ir::TermKind::Return:
        os << (t.has_value && t.value
                   ? "  return " + render(*t.value, vm, 0) + ";\n"
                   : "  return;\n");
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

std::string emit_expr(const ir::Expr &e, const vars::VarMap &vm) {
  return render(e, vm, 0);
}

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
    os << (st.is_dag() ? st.run() : emit_goto(fn, vm));
  }

  os << "}\n";
  return os.str();
}

} // namespace emit
