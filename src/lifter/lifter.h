// Lifter: M-CORE insn_t (decoded by IDA 9.4's built-in MCORE module) -> IR.
// This is the only IDA-dependent part of the decompiler core.
#pragma once

#include <pro.h>

struct func_t;
namespace ir { struct Function; }

namespace mcore {

// Lift one function into IR. Returns false and fills `err` on failure.
bool lift_function(func_t *pfn, ir::Function &out, qstring &err);

} // namespace mcore
