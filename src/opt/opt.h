// Dataflow cleanup over a function's (currently linear) statement list:
// constant/copy propagation, constant folding, and dead-assignment elimination.
// Pure, unit-testable. Grows into block-level dataflow in later milestones.
#pragma once

namespace ir { struct Function; }

namespace opt {

// Simplify `fn` in place.
void simplify(ir::Function &fn);

} // namespace opt
