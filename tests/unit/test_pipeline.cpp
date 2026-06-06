// Offline unit tests for the pure decompiler pipeline (IR -> vars -> opt -> emit).
// No IDA dependency.
#include "test_framework.h"

#include "ir/ir.h"
#include "vars/vars.h"
#include "opt/opt.h"
#include "emit/emit.h"

#include <string>

namespace {

bool contains(const std::string &hay, const std::string &needle) {
  return hay.find(needle) != std::string::npos;
}

// Full pipeline: recover vars (pre-opt), simplify, emit.
std::string decompile(ir::Function fn) {
  vars::VarMap vm = vars::analyze(fn);
  opt::simplify(fn);
  return emit::emit_c(fn, vm);
}

} // namespace

TEST("emit constant expression") {
  vars::VarMap vm;
  CHECK(emit::emit_expr(*ir::constant(42), vm) == "42");
}

TEST("emit minimal parentheses by precedence") {
  vars::VarMap vm;
  // (a + b) -- top level, no outer parens
  auto add = ir::binop(ir::BinOp::Add, ir::reg(2), ir::reg(3));
  CHECK(emit::emit_expr(*add, vm) == "r2 + r3");
  // a + b * c  -- no parens around the multiply
  auto e = ir::binop(ir::BinOp::Add, ir::reg(2),
                     ir::binop(ir::BinOp::Mul, ir::reg(3), ir::reg(4)));
  CHECK(emit::emit_expr(*e, vm) == "r2 + r3 * r4");
  // (a + b) * c  -- parens needed around the add
  auto e2 = ir::binop(ir::BinOp::Mul,
                      ir::binop(ir::BinOp::Add, ir::reg(2), ir::reg(3)),
                      ir::reg(4));
  CHECK(emit::emit_expr(*e2, vm) == "(r2 + r3) * r4");
}

TEST("const folding collapses arithmetic and drops dead assign") {
  ir::Function fn;
  fn.name = "f";
  fn.stmts.push_back(ir::assign(2, ir::binop(ir::BinOp::Add,
                                             ir::constant(40), ir::constant(2))));
  fn.stmts.push_back(ir::ret(ir::reg(2)));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "return 42;"));
  CHECK(!contains(c, "= 42"));
}

TEST("return42: no params, void signature") {
  ir::Function fn;
  fn.name = "f";
  fn.stmts.push_back(ir::assign(ir::kRegRet, ir::constant(42)));
  fn.stmts.push_back(ir::ret(ir::reg(ir::kRegRet)));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "int f(void)"));
  CHECK(contains(c, "return 42;"));
}

TEST("add(a,b): r2,r3 recovered as parameters a1,a2") {
  // addu r2, r3 ; rts   ==>  int f(int a1, int a2) { a1 = a1 + a2; return a1; }
  ir::Function fn;
  fn.name = "f";
  fn.stmts.push_back(ir::assign(2, ir::binop(ir::BinOp::Add, ir::reg(2), ir::reg(3))));
  fn.stmts.push_back(ir::ret(ir::reg(2)));

  vars::VarMap vm = vars::analyze(fn);
  CHECK(vm.params.size() == 2);
  CHECK(vm.name_of(2) == "a1");
  CHECK(vm.name_of(3) == "a2");

  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "int f(int a1, int a2)"));
  CHECK(contains(c, "a1 = a1 + a2;"));
  CHECK(contains(c, "return a1;"));
  CHECK(!contains(c, "r2"));
  CHECK(!contains(c, "r3"));
}

TEST("local register gets a v-name") {
  // r4 written then returned (r4 not an arg reg input) -> local v1
  ir::Function fn;
  fn.stmts.push_back(ir::assign(4, ir::reg(2)));   // r4 = a1
  fn.stmts.push_back(ir::ret(ir::reg(4)));
  vars::VarMap vm = vars::analyze(fn);
  CHECK(vm.name_of(4) == "v1");
  CHECK(vm.name_of(2) == "a1");   // r2 read before write -> param
}

TEST_MAIN()
