#include "emit/emit.h"

#include "ir/ir.h"

#include <sstream>

namespace emit {

namespace {

const char *binop_sym(ir::BinOp op) {
  switch (op) {
    case ir::BinOp::Add: return "+";
    case ir::BinOp::Sub: return "-";
    case ir::BinOp::Mul: return "*";
    case ir::BinOp::And: return "&";
    case ir::BinOp::Or: return "|";
    case ir::BinOp::Xor: return "^";
    case ir::BinOp::Shl: return "<<";
    case ir::BinOp::Shr: return ">>";
    case ir::BinOp::Sar: return ">>";  // signed shift; type handling comes later
    case ir::BinOp::CmpLt: return "<";
    case ir::BinOp::CmpHs: return ">=";
    case ir::BinOp::CmpNe: return "!=";
    case ir::BinOp::CmpEq: return "==";
  }
  return "?";
}

std::string reg_name(int r) {
  if (r == ir::kRegSP) return "sp";
  if (r == ir::kRegLR) return "lr";
  return "r" + std::to_string(r);
}

} // namespace

std::string emit_expr(const ir::Expr &e) {
  switch (e.kind) {
    case ir::ExprKind::Const: {
      std::ostringstream os;
      os << e.value;
      return os.str();
    }
    case ir::ExprKind::Reg:
      return reg_name(e.reg);
    case ir::ExprKind::UnOp: {
      const char *op = e.unop == ir::UnOp::Neg ? "-" : "~";
      return std::string(op) + "(" + (e.a ? emit_expr(*e.a) : "") + ")";
    }
    case ir::ExprKind::BinOp: {
      std::string lhs = e.a ? emit_expr(*e.a) : "";
      std::string rhs = e.b ? emit_expr(*e.b) : "";
      return "(" + lhs + " " + binop_sym(e.binop) + " " + rhs + ")";
    }
  }
  return "?";
}

std::string emit_c(const ir::Function &fn) {
  std::ostringstream os;
  const std::string name = fn.name.empty() ? "sub" : fn.name;
  os << "int " << name << "(void)\n{\n";
  for (const auto &s : fn.stmts) {
    switch (s.kind) {
      case ir::StmtKind::Assign:
        os << "  " << reg_name(s.dst_reg) << " = "
           << (s.expr ? emit_expr(*s.expr) : "?") << ";\n";
        break;
      case ir::StmtKind::Return:
        if (s.has_value && s.expr)
          os << "  return " << emit_expr(*s.expr) << ";\n";
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
