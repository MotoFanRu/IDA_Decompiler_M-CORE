// Variable recovery & naming: map M-CORE registers to readable C variables.
//
// Inputs (registers read before being written) that are ABI argument registers
// (r2..r7) become parameters a1..aN; other used GP registers become locals v1..;
// r0/r15 render as sp/lr. Pure, unit-testable. Run on the lifted IR BEFORE the
// optimizer removes assignments (so input detection is accurate).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace ir { struct Function; }

namespace vars {

struct VarMap {
  std::map<int, std::string> name;  // register number -> C variable name
  std::vector<int> params;          // argument registers, in signature order

  std::string name_of(int reg) const;
};

VarMap analyze(const ir::Function &fn);

} // namespace vars
