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

// Split intra-block register live ranges: when a register holds several distinct
// values in a block (reassigned with the old value still used), give each value
// its own variable. Fixes value confusion (e.g. a compared call result reused
// before the branch) and improves naming. Run after recover_stack, before vars.
void split_ranges(ir::Function &fn);

// Simplify `fn` in place.
void simplify(ir::Function &fn);

// Readability pass: copy/constant propagation, single-use inlining of pure
// temporaries, and liveness-based dead-store elimination. Run after simplify().
void inline_locals(ir::Function &fn);

} // namespace opt
