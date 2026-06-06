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
inline constexpr int kRenameBase = 0x10000;  // split live-ranges: fresh local versions
inline constexpr int kStackBase = 0x40000;   // stack slots: kStackBase + byte offset
inline constexpr int kRegDiscard = -1;       // Assign dst: evaluate expr only (void call)

enum class ExprKind { Const, Reg, BinOp, UnOp, Load, Call, Cast, Select };

enum class BinOp {
  Add, Sub, Mul, Div,
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
  ExprPtr a, b;             // operands (Load: a=address; Call: a=indirect target; Cast: a=inner)
  int size = 0;             // Load/Cast access size in bytes
  bool is_signed = false;   // Cast signedness
  std::string name;         // Call target name / named Const (symbol)
  std::vector<ExprPtr> args;// Call arguments
};

inline ExprPtr constant(int64_t v) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Const;
  e->value = v;
  return e;
}
inline ExprPtr const_named(int64_t v, std::string name) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Const;
  e->value = v;
  e->name = std::move(name);
  return e;
}
inline ExprPtr cast(int size, bool is_signed, ExprPtr a) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Cast;
  e->size = size;
  e->is_signed = is_signed;
  e->a = std::move(a);
  return e;
}
// Conditional move / ternary: cond ? then_val : else_val.
inline ExprPtr select(ExprPtr cond, ExprPtr then_val, ExprPtr else_val) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Select;
  e->a = std::move(cond);
  e->b = std::move(then_val);
  e->args.push_back(std::move(else_val));
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
inline ExprPtr load(ExprPtr addr, int size) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Load;
  e->a = std::move(addr);
  e->size = size;
  return e;
}
inline ExprPtr call(std::string name, std::vector<ExprPtr> args) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Call;
  e->name = std::move(name);
  e->args = std::move(args);
  return e;
}
inline ExprPtr call_indirect(ExprPtr target, std::vector<ExprPtr> args) {
  auto e = std::make_shared<Expr>();
  e->kind = ExprKind::Call;
  e->a = std::move(target);
  e->args = std::move(args);
  return e;
}

enum class StmtKind { Assign, Store, Unknown };

struct Stmt {
  StmtKind kind;
  int dst_reg = 0;          // Assign
  ExprPtr expr;             // Assign rhs / Store value
  ExprPtr addr;             // Store address
  int size = 4;             // Store access size in bytes
  uint32_t ea = 0;          // source address
  std::string text;         // Unknown: disassembly text
};

inline Stmt assign(int dst, ExprPtr e, uint32_t ea = 0) {
  return Stmt{StmtKind::Assign, dst, std::move(e), nullptr, 4, ea, {}};
}
inline Stmt store(ExprPtr addr, ExprPtr value, int size, uint32_t ea = 0) {
  return Stmt{StmtKind::Store, 0, std::move(value), std::move(addr), size, ea, {}};
}
inline Stmt unknown(uint32_t ea, std::string text) {
  return Stmt{StmtKind::Unknown, 0, nullptr, nullptr, 4, ea, std::move(text)};
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
