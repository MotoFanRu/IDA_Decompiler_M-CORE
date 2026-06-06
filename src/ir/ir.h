// Intermediate representation for the M-CORE decompiler (Path B).
//
// Pure, no IDA dependency, so the emitter and optimizer can be unit-tested
// offline. The lifter (IDA-dependent) builds these structures from insn_t.
//
// B1 scope: a single linear list of statements per function (constants, register
// assignments, return). Expressions form a small tree. A CFG of blocks is added
// in later milestones; the types here are intended to grow into that.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ir {

// M-CORE register numbers match the module's mcore_registers enum (r0..r15 == 0..15).
inline constexpr int kRegSP = 0;   // r0
inline constexpr int kRegRet = 2;  // r2 holds the return value (ABI)
inline constexpr int kRegLR = 15;  // r15 link register

enum class ExprKind { Const, Reg, BinOp, UnOp };

enum class BinOp {
  Add, Sub, Mul,
  And, Or, Xor,
  Shl, Shr, Sar,
  CmpLt, CmpHs, CmpNe, CmpEq,
};

enum class UnOp { Neg, Not };

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
  ExprKind kind;
  int64_t value = 0;        // Const
  int reg = 0;              // Reg
  BinOp binop{};            // BinOp
  UnOp unop{};              // UnOp
  ExprPtr a, b;             // operands
};

inline ExprPtr constant(int64_t v) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Const;
  e->value = v;
  return e;
}
inline ExprPtr reg(int r) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Reg;
  e->reg = r;
  return e;
}
inline ExprPtr binop(BinOp op, ExprPtr a, ExprPtr b) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::BinOp;
  e->binop = op;
  e->a = std::move(a);
  e->b = std::move(b);
  return e;
}
inline ExprPtr unop(UnOp op, ExprPtr a) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::UnOp;
  e->unop = op;
  e->a = std::move(a);
  return e;
}

enum class StmtKind {
  Assign,   // dst_reg = expr
  Return,   // return [expr]
  Unknown,  // un-lifted instruction (placeholder, keeps output honest)
};

struct Stmt {
  StmtKind kind;
  int dst_reg = 0;          // Assign
  ExprPtr expr;             // Assign rhs / Return value (may be null)
  bool has_value = false;   // Return carries a value?
  uint32_t ea = 0;          // source address
  std::string text;         // Unknown: disassembly text for the comment
};

inline Stmt assign(int dst, ExprPtr e, uint32_t ea = 0) {
  return Stmt{StmtKind::Assign, dst, std::move(e), false, ea, {}};
}
inline Stmt ret(ExprPtr e, uint32_t ea = 0) {
  const bool has_value = e != nullptr;  // compute before moving e
  return Stmt{StmtKind::Return, 0, std::move(e), has_value, ea, {}};
}
inline Stmt unknown(uint32_t ea, std::string text) {
  return Stmt{StmtKind::Unknown, 0, nullptr, false, ea, std::move(text)};
}

struct Function {
  uint32_t entry = 0;
  std::string name;
  std::vector<Stmt> stmts;  // B1: linear; CFG of blocks comes later
};

} // namespace ir
