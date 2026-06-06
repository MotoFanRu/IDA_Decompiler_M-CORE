// Lifter: M-CORE insn_t (decoded by the active M*CORE processor module) -> IR.
// This is the only IDA-dependent part of the decompiler core.
#pragma once

#include <pro.h>

struct func_t;
namespace ir { struct Function; }

namespace mcore {

// Lift one function into IR. Returns false and fills `err` on failure.
// B1 scope: linear walk; only movi and rts(=jmp r15) are lifted, everything
// else becomes an Unknown placeholder carrying the disassembly text.
bool lift_function(func_t *pfn, ir::Function &out, qstring &err);

} // namespace mcore
