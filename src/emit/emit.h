// C pseudocode emitter: ir::Function + recovered variables -> text.
// Pure, unit-testable. Precedence-aware (minimal parentheses).
#pragma once

#include <string>

namespace ir { struct Function; struct Expr; }
namespace vars { struct VarMap; }

namespace emit {

// Render a whole function as C-like pseudocode.
std::string emit_c(const ir::Function &fn, const vars::VarMap &vm);

// Render a single expression (exposed for testing).
std::string emit_expr(const ir::Expr &e, const vars::VarMap &vm);

} // namespace emit
