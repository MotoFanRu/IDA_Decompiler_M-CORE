// Intermediate representation for the M-CORE decompiler (Path B).
//
// Pure, no IDA dependency, so vars/opt/emit/structuring can be unit-tested
// offline. The lifter (IDA-dependent) builds these structures from insn_t.
//
// A Function is a CFG of basic Blocks. Each block has a list of Statements and a
// Terminator (fallthrough / goto / conditional branch / return). Targets are
// block entry addresses.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ir {

// M-CORE register numbers match the module's mcore_registers enum (r0..r15 == 0..15).
inline constexpr int kRegSP = 0;    // r0
inline constexpr int kRegRet = 2;   // r2 holds the return value (ABI)
inline constexpr int kRegLR = 15;   // r15 link register
inline constexpr int kRegC = 100;   // synthetic 1-bit condition (PSR C bit)

enum class ExprKind { Const, Reg, BinOp, UnOp };

enum class BinOp {
  Add, Sub, Mul,
  And, Or, Xor,
  Shl, Shr, Sar,
  CmpLt, CmpHs, CmpNe, CmpEq,
};

enum class UnOp { Neg, Not, LNot };  // -x, ~x, !x

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

enum class StmtKind { Assign, Unknown };

struct Stmt {
  StmtKind kind;
  int dst_reg = 0;          // Assign
  ExprPtr expr;             // Assign rhs
  uint32_t ea = 0;          // source address
  std::string text;         // Unknown: disassembly text
};

inline Stmt assign(int dst, ExprPtr e, uint32_t ea = 0) {
  return Stmt{StmtKind::Assign, dst, std::move(e), ea, {}};
}
inline Stmt unknown(uint32_t ea, std::string text) {
  return Stmt{StmtKind::Unknown, 0, nullptr, ea, std::move(text)};
}

enum class TermKind {
  Fallthrough,  // -> target
  Goto,         // -> target (unconditional branch)
  CondBranch,   // cond ? taken : fallthrough
  Return,       // return [value]
};

struct Terminator {
  TermKind kind = TermKind::Fallthrough;
  uint32_t target = 0;       // Fallthrough/Goto/CondBranch taken target
  uint32_t fallthrough = 0;  // CondBranch not-taken target
  ExprPtr cond;              // CondBranch condition (taken when true)
  ExprPtr value;             // Return value (may be null)
  bool has_value = false;    // Return carries a value?
  uint32_t ea = 0;
};

struct Block {
  uint32_t entry = 0;            // block address (its id)
  std::vector<Stmt> stmts;
  Terminator term;
};

struct Function {
  uint32_t entry = 0;            // entry block address
  std::string name;
  std::vector<Block> blocks;     // blocks[0] is the entry block

  Block *block_at(uint32_t ea) {
    for (auto &b : blocks)
      if (b.entry == ea) return &b;
    return nullptr;
  }
  const Block *block_at(uint32_t ea) const {
    for (const auto &b : blocks)
      if (b.entry == ea) return &b;
    return nullptr;
  }
};

} // namespace ir
