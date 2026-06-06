// Dataflow cleanup over a function's (currently linear) statement list:
// constant/copy propagation, constant folding, and dead-assignment elimination.
// Pure, unit-testable. Grows into block-level dataflow in later milestones.
#pragma once

namespace ir { struct Function; }

namespace opt {

// Recover the stack frame: turn sp-relative loads/stores into named stack-slot
// variables and drop prologue/epilogue boilerplate (sp adjustments, lr save/
// restore, stm/ldm register save/restore). Run before simplify().
void recover_stack(ir::Function &fn);

// Simplify `fn` in place.
void simplify(ir::Function &fn);

} // namespace opt
