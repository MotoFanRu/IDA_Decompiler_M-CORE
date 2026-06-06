// Offline unit tests for the pure decompiler pipeline (IR -> opt -> emit).
// No IDA dependency.
#include "test_framework.h"

#include "ir/ir.h"
#include "opt/opt.h"
#include "emit/emit.h"

#include <string>

namespace {
bool contains(const std::string &hay, const std::string &needle) {
  return hay.find(needle) != std::string::npos;
}
}

TEST("emit constant expression") {
  CHECK(emit::emit_expr(*ir::constant(42)) == "42");
}

TEST("emit binop expression") {
  auto e = ir::binop(ir::BinOp::Add, ir::constant(1), ir::reg(3));
  CHECK(emit::emit_expr(*e) == "(1 + r3)");
}

TEST("const folding collapses arithmetic") {
  ir::Function fn;
  fn.name = "f";
  fn.stmts.push_back(ir::assign(2, ir::binop(ir::BinOp::Add,
                                             ir::constant(40), ir::constant(2))));
  fn.stmts.push_back(ir::ret(ir::reg(2)));
  opt::simplify(fn);
  std::string c = emit::emit_c(fn);
  CHECK(contains(c, "return 42;"));
  CHECK(!contains(c, "r2"));      // assignment propagated + eliminated
}

TEST("B1 scenario: movi r2,42 ; rts -> return 42") {
  // Mirrors the lifter output for the return42 fixture.
  ir::Function fn;
  fn.name = "f";
  fn.stmts.push_back(ir::assign(ir::kRegRet, ir::constant(42)));
  fn.stmts.push_back(ir::ret(ir::reg(ir::kRegRet)));
  opt::simplify(fn);
  std::string c = emit::emit_c(fn);
  CHECK(contains(c, "int f(void)"));
  CHECK(contains(c, "return 42;"));
  CHECK(!contains(c, "= 42"));    // dead assignment removed
}

TEST("dead assignment is eliminated") {
  ir::Function fn;
  fn.stmts.push_back(ir::assign(3, ir::constant(5)));  // never used
  fn.stmts.push_back(ir::ret(ir::constant(0)));
  opt::simplify(fn);
  std::string c = emit::emit_c(fn);
  CHECK(!contains(c, "r3"));
  CHECK(contains(c, "return 0;"));
}

TEST_MAIN()
