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

std::string decompile(ir::Function fn) {
  opt::recover_stack(fn);
  opt::split_ranges(fn);
  vars::VarMap vm = vars::analyze(fn);
  opt::simplify(fn);
  opt::inline_locals(fn);
  return emit::emit_c(fn, vm);
}

// --- IR builder helpers ----------------------------------------------------
ir::Terminator ret_t(ir::ExprPtr v) {
  ir::Terminator t;
  t.kind = ir::TermKind::Return;
  t.value = v;
  t.has_value = v != nullptr;
  return t;
}
ir::Terminator fall_t(uint32_t to) {
  ir::Terminator t;
  t.kind = ir::TermKind::Fallthrough;
  t.target = to;
  return t;
}
ir::Terminator goto_t(uint32_t to) {
  ir::Terminator t;
  t.kind = ir::TermKind::Goto;
  t.target = to;
  return t;
}
ir::Terminator cbr_t(ir::ExprPtr cond, uint32_t taken, uint32_t fth) {
  ir::Terminator t;
  t.kind = ir::TermKind::CondBranch;
  t.cond = cond;
  t.target = taken;
  t.fallthrough = fth;
  return t;
}
// Single-block function: stmts + return value.
ir::Function one_block(std::vector<ir::Stmt> stmts, ir::ExprPtr retval,
                       const char *name = "f") {
  ir::Function fn;
  fn.name = name;
  fn.entry = 0;
  ir::Block b;
  b.entry = 0;
  b.stmts = std::move(stmts);
  b.term = ret_t(retval);
  fn.blocks.push_back(std::move(b));
  return fn;
}

} // namespace

TEST("emit minimal parentheses by precedence") {
  vars::VarMap vm;
  auto add = ir::binop(ir::BinOp::Add, ir::reg(2), ir::reg(3));
  CHECK(emit::emit_expr(*add, vm) == "r2 + r3");
  auto e = ir::binop(ir::BinOp::Add, ir::reg(2),
                     ir::binop(ir::BinOp::Mul, ir::reg(3), ir::reg(4)));
  CHECK(emit::emit_expr(*e, vm) == "r2 + r3 * r4");
  auto e2 = ir::binop(ir::BinOp::Mul,
                      ir::binop(ir::BinOp::Add, ir::reg(2), ir::reg(3)), ir::reg(4));
  CHECK(emit::emit_expr(*e2, vm) == "(r2 + r3) * r4");
}

TEST("return42: no params, void signature") {
  auto fn = one_block({ir::assign(ir::kRegRet, ir::constant(42))}, ir::reg(ir::kRegRet));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "int f(void)"));
  CHECK(contains(c, "return 42;"));
}

TEST("const folding collapses arithmetic and drops dead assign") {
  auto fn = one_block(
      {ir::assign(2, ir::binop(ir::BinOp::Add, ir::constant(40), ir::constant(2)))},
      ir::reg(2));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "return 42;"));
  CHECK(!contains(c, "= 42"));
}

TEST("add(a,b): r2,r3 recovered as parameters a1,a2") {
  ir::Function fn = one_block(
      {ir::assign(2, ir::binop(ir::BinOp::Add, ir::reg(2), ir::reg(3)))}, ir::reg(2));

  vars::VarMap vm = vars::analyze(fn);
  CHECK(vm.params.size() == 2);
  CHECK(vm.name_of(2) == "a1");
  CHECK(vm.name_of(3) == "a2");

  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "int f(int a1, int a2)"));
  CHECK(contains(c, "a1 = a1 + a2;"));
  CHECK(contains(c, "return a1;"));
  CHECK(!contains(c, "r2"));
}

TEST("local register gets a v-name") {
  ir::Function fn = one_block({ir::assign(4, ir::reg(2))}, ir::reg(4));
  vars::VarMap vm = vars::analyze(fn);
  CHECK(vm.name_of(4) == "v1");
  CHECK(vm.name_of(2) == "a1");
}

TEST("if-then structuring (max): cmplt + bf") {
  // B0: C = r2 < r3 ; bf B2     (bf taken when !C)
  // B1: r2 = r3 ; fall to B2
  // B2: return r2
  ir::Function fn;
  fn.name = "f";
  fn.entry = 0;

  ir::Block b0;
  b0.entry = 0;
  b0.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpLt, ir::reg(2), ir::reg(3))));
  b0.term = cbr_t(ir::unop(ir::UnOp::LNot, ir::reg(ir::kRegC)), /*taken=*/4, /*fth=*/2);
  fn.blocks.push_back(std::move(b0));

  ir::Block b1;
  b1.entry = 2;
  b1.stmts.push_back(ir::assign(2, ir::reg(3)));
  b1.term = fall_t(4);
  fn.blocks.push_back(std::move(b1));

  ir::Block b2;
  b2.entry = 4;
  b2.term = ret_t(ir::reg(2));
  fn.blocks.push_back(std::move(b2));

  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "int f(int a1, int a2)"));
  CHECK(contains(c, "if (a1 < a2) {"));
  CHECK(contains(c, "a1 = a2;"));
  CHECK(contains(c, "return a1;"));
  CHECK(!contains(c, "goto"));
  CHECK(!contains(c, "r100"));   // C bit fully inlined away
}

TEST("load renders as typed pointer dereference") {
  ir::Function fn = one_block({ir::assign(2, ir::load(ir::reg(2), 4))}, ir::reg(2));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "a1 = *(int *)(a1);"));
  CHECK(contains(c, "return a1;"));
}

TEST("store renders as typed pointer assignment") {
  ir::Function fn;
  fn.name = "f";
  fn.entry = 0;
  ir::Block b;
  b.entry = 0;
  b.stmts.push_back(ir::store(ir::reg(2), ir::reg(3), 4));
  b.term = ret_t(ir::reg(2));
  fn.blocks.push_back(std::move(b));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "*(int *)(a1) = a2;"));
}

TEST("scaled load/store render as array indexing") {
  vars::VarMap vm;
  // *(int*)(r2 + r3*4)  ->  r2[r3]
  auto addr = ir::binop(ir::BinOp::Add, ir::reg(2),
                        ir::binop(ir::BinOp::Mul, ir::reg(3), ir::constant(4)));
  CHECK(emit::emit_expr(*ir::load(addr, 4), vm) == "r2[r3]");
  // non-matching scale stays as a typed deref
  auto addr2 = ir::binop(ir::BinOp::Add, ir::reg(2),
                         ir::binop(ir::BinOp::Mul, ir::reg(3), ir::constant(2)));
  CHECK(emit::emit_expr(*ir::load(addr2, 4), vm) == "*(int *)(r2 + r3 * 2)");
}

TEST("byte load uses unsigned char pointer") {
  ir::Function fn = one_block({ir::assign(2, ir::load(ir::reg(2), 1))}, ir::reg(2));
  CHECK(contains(decompile(std::move(fn)), "*(unsigned char *)(a1)"));
}

TEST("call with args; result assigned") {
  std::vector<ir::ExprPtr> args = {ir::reg(2)};
  ir::Function fn = one_block({ir::assign(2, ir::call("g", args))}, ir::reg(2));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "a1 = g(a1);"));
  CHECK(contains(c, "return a1;"));
}

TEST("call with unused result is not eliminated") {
  ir::Function fn = one_block({ir::assign(3, ir::call("g", {}))}, ir::constant(0));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "g()"));     // side effect preserved
  CHECK(contains(c, "return 0;"));
}

TEST("dead call result renders as a void-style statement") {
  // r2 = foo() ; r2 = bar() ; return r2   -> foo();  (its result is dead)
  std::vector<ir::Stmt> stmts;
  stmts.push_back(ir::assign(2, ir::call("foo", {})));
  stmts.push_back(ir::assign(2, ir::call("bar", {})));
  ir::Function fn = one_block(std::move(stmts), ir::reg(2));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "foo();"));      // dead result -> bare call
  CHECK(!contains(c, "= foo("));     // no assignment of the dead result
}

TEST("compared call result is kept when its register is reused (live-range split)") {
  // r3 = a1 ; r2 = f() ; C = (r2 != 0) ; r2 = r3 (restore) ; if (C) body
  // The call result must be kept and drive the condition, not be lost/confused.
  ir::Function fn; fn.name = "g"; fn.entry = 0;
  ir::Block b0; b0.entry = 0;
  b0.stmts.push_back(ir::assign(3, ir::reg(2)));
  b0.stmts.push_back(ir::assign(2, ir::call("f", {})));
  b0.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpNe, ir::reg(2), ir::constant(0))));
  b0.stmts.push_back(ir::assign(2, ir::reg(3)));
  b0.term = cbr_t(ir::reg(ir::kRegC), /*taken=*/2, /*fth=*/4);
  fn.blocks.push_back(std::move(b0));
  ir::Block b1; b1.entry = 2;
  b1.stmts.push_back(ir::assign(2, ir::constant(7)));
  b1.term = fall_t(4);
  fn.blocks.push_back(std::move(b1));
  ir::Block b2; b2.entry = 4;
  b2.term = ret_t(ir::reg(2));
  fn.blocks.push_back(std::move(b2));

  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "= f();"));    // call result kept (not discarded as a void call)
  CHECK(contains(c, "!= 0"));      // a real non-null check on that result
  CHECK(!contains(c, "0 != 0"));
}

TEST("C condition survives the compared register being overwritten") {
  // r2 = *(r5) ; r8 = r2 ; C = (r2 != 0) ; r2 = 0 ; if (C) {body} ; return r2
  // The condition must use the saved copy (r8), NOT the now-zero r2 -> never '0 != 0'.
  ir::Function fn; fn.name = "f"; fn.entry = 0;
  ir::Block b0; b0.entry = 0;
  b0.stmts.push_back(ir::assign(2, ir::load(ir::reg(5), 4)));
  b0.stmts.push_back(ir::assign(8, ir::reg(2)));
  b0.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpNe, ir::reg(2), ir::constant(0))));
  b0.stmts.push_back(ir::assign(2, ir::constant(0)));
  b0.term = cbr_t(ir::reg(ir::kRegC), /*taken=*/2, /*fth=*/4);
  fn.blocks.push_back(std::move(b0));
  ir::Block b1; b1.entry = 2;
  b1.stmts.push_back(ir::assign(2, ir::constant(7)));
  b1.term = fall_t(4);
  fn.blocks.push_back(std::move(b1));
  ir::Block b2; b2.entry = 4;
  b2.term = ret_t(ir::reg(2));
  fn.blocks.push_back(std::move(b2));

  std::string c = decompile(std::move(fn));
  CHECK(!contains(c, "0 != 0"));   // the bug: must not collapse to a constant compare
  CHECK(contains(c, "!= 0"));      // a real non-null check remains
}

TEST("bitwise-not and and-not render") {
  vars::VarMap vm;
  CHECK(emit::emit_expr(*ir::unop(ir::UnOp::Not, ir::reg(2)), vm) == "~r2");
  auto andn = ir::binop(ir::BinOp::And, ir::reg(2), ir::unop(ir::UnOp::Not, ir::reg(3)));
  CHECK(emit::emit_expr(*andn, vm) == "r2 & ~r3");
}

TEST("un-lifted instruction renders as inline asm") {
  ir::Function fn = one_block({ir::unknown(0, "addc    r3, r13")}, ir::reg(2));
  CHECK(contains(decompile(std::move(fn)), "__asm { addc    r3, r13 }"));
}

TEST("cast renders and folds") {
  vars::VarMap vm;
  CHECK(emit::emit_expr(*ir::cast(1, false, ir::reg(2)), vm) == "(unsigned char)r2");
  CHECK(emit::emit_expr(*ir::cast(1, true, ir::reg(2)), vm) == "(char)r2");
  // fold cast of a constant: (unsigned char)0x1FF == 0xFF == 255
  ir::Function fn = one_block({ir::assign(2, ir::cast(1, false, ir::constant(0x1FF)))}, ir::reg(2));
  CHECK(contains(decompile(std::move(fn)), "return 255;"));
}

TEST("named constant (lrw symbol) renders as the symbol") {
  vars::VarMap vm;
  CHECK(emit::emit_expr(*ir::const_named(0x10040A30, "dword_10040A30"), vm) == "dword_10040A30");
}

TEST("movi + shift idiom folds to one constant (rendered hex)") {
  // movi r2,3 ; lsli r2,16  ->  r2 = 0x30000
  ir::Function fn = one_block(
      {ir::assign(2, ir::constant(3)),
       ir::assign(2, ir::binop(ir::BinOp::Shl, ir::reg(2), ir::constant(16)))},
      ir::reg(2));
  CHECK(contains(decompile(std::move(fn)), "return 0x30000;"));
}

TEST("large constant renders hex, small decimal") {
  vars::VarMap vm;
  CHECK(emit::emit_expr(*ir::constant(0x1000), vm) == "0x1000");
  CHECK(emit::emit_expr(*ir::constant(42), vm) == "42");
  CHECK(emit::emit_expr(*ir::constant(-1), vm) == "-1");
}

TEST("conditional move renders as a ternary with resolved condition") {
  // C = r2 < r3 ; movt r4,r5 (r4 = C ? r5 : r4) ; return r4
  ir::Function fn; fn.name = "f"; fn.entry = 0;
  ir::Block b; b.entry = 0;
  b.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpLt, ir::reg(2), ir::reg(3))));
  b.stmts.push_back(ir::assign(4, ir::select(ir::reg(ir::kRegC), ir::reg(5), ir::reg(4))));
  b.term = ret_t(ir::reg(4));
  fn.blocks.push_back(std::move(b));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "a1 < a2 ?"));   // condition resolved into the ternary
  CHECK(!contains(c, "cond"));
  CHECK(!contains(c, "r100"));
}

TEST("locals are declared") {
  // v = *a1 (load, not inlinable); used twice -> kept as a declared local.
  std::vector<ir::Stmt> stmts;
  stmts.push_back(ir::assign(4, ir::load(ir::reg(2), 4)));   // v1 = *(int*)a1
  stmts.push_back(ir::store(ir::reg(3), ir::reg(4), 4));      // *(a2) = v1   (use 1)
  ir::Function fn = one_block(std::move(stmts), ir::reg(4));   // return v1   (use 2)
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "int v1;"));
  CHECK(contains(c, "v1 = *(int *)(a1);"));
  CHECK(contains(c, "int f(int a1, int a2)"));
}

TEST("single-use temporary is inlined") {
  // v_tmp = a + b ; return v_tmp   ->   return a1 + a2;
  std::vector<ir::Stmt> stmts;
  stmts.push_back(ir::assign(4, ir::binop(ir::BinOp::Add, ir::reg(2), ir::reg(3))));
  ir::Function fn = one_block(std::move(stmts), ir::reg(4));
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "return a1 + a2;"));
  CHECK(!contains(c, "v1"));   // temporary eliminated
}

TEST("dead overwriting store is removed") {
  // r4 = 81 ; r4 = a1 + 1 ; return r4   (first def is dead)
  std::vector<ir::Stmt> stmts;
  stmts.push_back(ir::assign(4, ir::constant(81)));
  stmts.push_back(ir::assign(4, ir::binop(ir::BinOp::Add, ir::reg(2), ir::constant(1))));
  ir::Function fn = one_block(std::move(stmts), ir::reg(4));
  std::string c = decompile(std::move(fn));
  CHECK(!contains(c, "81"));            // dead 'r4 = 81' gone
  CHECK(contains(c, "return a1 + 1;")); // inlined
}

TEST("multi-use arithmetic is NOT duplicated") {
  // v = a + b (used twice): keep as a statement, do not inline twice
  std::vector<ir::Stmt> stmts;
  stmts.push_back(ir::assign(4, ir::binop(ir::BinOp::Add, ir::reg(2), ir::reg(3))));
  stmts.push_back(ir::store(ir::reg(5), ir::reg(4), 4));  // use 1
  ir::Function fn = one_block(std::move(stmts), ir::reg(4));  // use 2 (return)
  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "v1 = a1 + a2;"));   // kept once
}

TEST("cross-block C-bit resolves in a later block's branch") {
  // B0: C = r2 < r3 ; fallthrough B1
  // B1: bt B3        ; fallthrough B2   (branch reads C set in B0)
  // B2: r2 = 1       ; fallthrough B3
  // B3: return r2
  ir::Function fn;
  fn.name = "f";
  fn.entry = 0;
  ir::Block b0; b0.entry = 0;
  b0.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpLt, ir::reg(2), ir::reg(3))));
  b0.term = fall_t(2);
  fn.blocks.push_back(std::move(b0));
  ir::Block b1; b1.entry = 2;
  b1.term = cbr_t(ir::reg(ir::kRegC), /*taken=*/6, /*fth=*/4);
  fn.blocks.push_back(std::move(b1));
  ir::Block b2; b2.entry = 4;
  b2.stmts.push_back(ir::assign(2, ir::constant(1)));
  b2.term = fall_t(6);
  fn.blocks.push_back(std::move(b2));
  ir::Block b3; b3.entry = 6;
  b3.term = ret_t(ir::reg(2));
  fn.blocks.push_back(std::move(b3));

  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "a1 < a2"));   // condition resolved across blocks
  CHECK(!contains(c, "cond"));     // C bit fully inlined, not leaked
  CHECK(!contains(c, "r100"));
}

TEST("stack frame recovery: prologue/epilogue hidden, slot named") {
  // sp=sp-16 ; *(sp+12)=lr ; r2=*(sp+20)(incoming stack arg) ; lr=*(sp+12) ; sp=sp+16 ; return r2
  std::vector<ir::Stmt> stmts;
  stmts.push_back(ir::assign(0, ir::binop(ir::BinOp::Sub, ir::reg(0), ir::constant(16))));
  stmts.push_back(ir::store(ir::binop(ir::BinOp::Add, ir::reg(0), ir::constant(12)), ir::reg(15), 4));
  stmts.push_back(ir::assign(2, ir::load(ir::binop(ir::BinOp::Add, ir::reg(0), ir::constant(20)), 4)));
  stmts.push_back(ir::assign(15, ir::load(ir::binop(ir::BinOp::Add, ir::reg(0), ir::constant(12)), 4)));
  stmts.push_back(ir::assign(0, ir::binop(ir::BinOp::Add, ir::reg(0), ir::constant(16))));
  ir::Function fn = one_block(std::move(stmts), ir::reg(2));

  std::string c = decompile(std::move(fn));
  CHECK(!contains(c, "sp"));         // no stack-pointer juggling
  CHECK(!contains(c, "lr"));         // no link-register save/restore
  CHECK(contains(c, "var_14"));      // stack slot (offset 20) named
  CHECK(contains(c, "return var_14;"));
}

TEST("pre-test while loop structuring") {
  // B0: C = r2 < r3 ; bf B2(exit)    (stay in loop while a<b)
  // B1: r2 = r2 + 1 ; goto B0        (back edge)
  // B2: return r2
  ir::Function fn;
  fn.name = "f";
  fn.entry = 0;

  ir::Block b0;
  b0.entry = 0;
  b0.stmts.push_back(ir::assign(ir::kRegC, ir::binop(ir::BinOp::CmpLt, ir::reg(2), ir::reg(3))));
  b0.term = cbr_t(ir::unop(ir::UnOp::LNot, ir::reg(ir::kRegC)), /*taken=*/8, /*fth=*/4);
  fn.blocks.push_back(std::move(b0));

  ir::Block b1;
  b1.entry = 4;
  b1.stmts.push_back(ir::assign(2, ir::binop(ir::BinOp::Add, ir::reg(2), ir::constant(1))));
  b1.term = goto_t(0);
  fn.blocks.push_back(std::move(b1));

  ir::Block b2;
  b2.entry = 8;
  b2.term = ret_t(ir::reg(2));
  fn.blocks.push_back(std::move(b2));

  std::string c = decompile(std::move(fn));
  CHECK(contains(c, "while (a1 < a2) {"));
  CHECK(contains(c, "a1 = a1 + 1;"));
  CHECK(contains(c, "return a1;"));
  CHECK(!contains(c, "goto"));
  CHECK(!contains(c, "while (1)"));
}

TEST_MAIN()
