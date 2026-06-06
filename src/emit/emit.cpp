#include "emit/emit.h"

#include "ir/ir.h"
#include "vars/vars.h"

#include <sstream>

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

constexpr int kAtomPrec = 100;
constexpr int kUnPrec = 14;

// Render `e` assuming it appears in a context requiring at least `min_prec`;
// wraps in parentheses only when its own precedence is lower.
std::string render(const ir::Expr &e, const vars::VarMap &vm, int min_prec) {
  switch (e.kind) {
    case ir::ExprKind::Const: {
      std::ostringstream os;
      os << e.value;
      return os.str();
    }
    case ir::ExprKind::Reg:
      return vm.name_of(e.reg);
    case ir::ExprKind::UnOp: {
      const char *op = e.unop == ir::UnOp::Neg ? "-" : "~";
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

  for (const auto &s : fn.stmts) {
    switch (s.kind) {
      case ir::StmtKind::Assign:
        os << "  " << vm.name_of(s.dst_reg) << " = "
           << (s.expr ? emit_expr(*s.expr, vm) : "?") << ";\n";
        break;
      case ir::StmtKind::Return:
        if (s.has_value && s.expr)
          os << "  return " << emit_expr(*s.expr, vm) << ";\n";
        else
          os << "  return;\n";
        break;
      case ir::StmtKind::Unknown:
        os << "  // " << s.text << "\n";
        break;
    }
  }

  os << "}\n";
  return os.str();
}

} // namespace emit
