// C pseudocode emitter: ir::Function -> text. Pure, unit-testable.
#pragma once

#include <string>

namespace ir { struct Function; struct Expr; }

namespace emit {

// Render a whole function as C-like pseudocode.
std::string emit_c(const ir::Function &fn);

// Render a single expression (exposed for testing).
std::string emit_expr(const ir::Expr &e);

} // namespace emit
