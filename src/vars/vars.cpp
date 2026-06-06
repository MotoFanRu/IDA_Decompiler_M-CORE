#include "vars/vars.h"

#include "ir/ir.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace vars {

namespace {

constexpr int kArgFirst = 2;  // r2
constexpr int kArgLast = 7;   // r7

bool is_arg_reg(int r) { return r >= kArgFirst && r <= kArgLast; }
bool is_gp_reg(int r) { return r >= 0 && r <= 15; }

void collect_regs(const ir::ExprPtr &e, std::vector<int> &out) {
  if (!e) return;
  switch (e->kind) {
    case ir::ExprKind::Const: break;
    case ir::ExprKind::Reg: out.push_back(e->reg); break;
    case ir::ExprKind::UnOp:
    case ir::ExprKind::Cast:
    case ir::ExprKind::Load: collect_regs(e->a, out); break;
    case ir::ExprKind::BinOp:
      collect_regs(e->a, out);
      collect_regs(e->b, out);
      break;
    case ir::ExprKind::Select:
    case ir::ExprKind::Call:
      collect_regs(e->a, out);  // cond / indirect target
      collect_regs(e->b, out);  // then-value (null for Call)
      for (const auto &arg : e->args) collect_regs(arg, out);
      break;
  }
}

} // namespace

std::string VarMap::name_of(int reg) const {
  auto it = name.find(reg);
  if (it != name.end()) return it->second;
  return "r" + std::to_string(reg);
}

VarMap analyze(const ir::Function &fn) {
  VarMap vm;

  std::set<int> written;
  std::vector<int> inputs;  // arg regs read before written, first-seen order
  std::set<int> input_set;
  std::set<int> used;

  auto note_reads = [&](const ir::ExprPtr &e) {
    std::vector<int> reads;
    collect_regs(e, reads);
    for (int r : reads) {
      used.insert(r);
      if (!written.count(r) && is_arg_reg(r) && !input_set.count(r)) {
        input_set.insert(r);
        inputs.push_back(r);
      }
    }
  };

  // Address-ordered linear scan (approximates read-before-write across the CFG;
  // precise liveness comes with a later milestone).
  for (const auto &b : fn.blocks) {
    for (const auto &s : b.stmts) {
      note_reads(s.expr);
      note_reads(s.addr);  // Store address
      if (s.kind == ir::StmtKind::Assign) {
        used.insert(s.dst_reg);
        written.insert(s.dst_reg);
      }
    }
    note_reads(b.term.cond);
    note_reads(b.term.value);
  }

  // Parameters: argument-register inputs, ordered by register number (r2 first).
  vm.params = inputs;
  std::sort(vm.params.begin(), vm.params.end());
  for (size_t i = 0; i < vm.params.size(); ++i)
    vm.name[vm.params[i]] = "a" + std::to_string(i + 1);

  // Locals: stack slots -> var_<off>; remaining used GP registers -> v1..
  int local_n = 0;
  for (int r : used) {  // std::set => ascending, deterministic
    if (vm.name.count(r)) continue;  // already a param
    if (r >= ir::kStackBase) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "var_%X", r - ir::kStackBase);
      vm.name[r] = buf;
      continue;
    }
    if (r == ir::kRegC) { vm.name[r] = "cond"; continue; }  // unresolved C bit
    if (!is_gp_reg(r)) continue;  // skip other control regs
    if (r == ir::kRegSP) { vm.name[r] = "sp"; continue; }
    if (r == ir::kRegLR) { vm.name[r] = "lr"; continue; }
    vm.name[r] = "v" + std::to_string(++local_n);
  }

  return vm;
}

} // namespace vars
