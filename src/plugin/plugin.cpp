// M-CORE decompiler plugin for IDA Pro 9 (self-contained; Path B).
//
// Runs on a database analysed by the M*CORE processor module. The pipeline is:
// insn_t -> IR (lifter) -> simplify (opt) -> C text (emit). Hex-Rays is not used
// (no decompiler backend loads for an unsupported processor).
#include <ida.hpp>
#include <idp.hpp>
#include <loader.hpp>
#include <kernwin.hpp>
#include <funcs.hpp>

#include "ir/ir.h"
#include "lifter/lifter.h"
#include "vars/vars.h"
#include "opt/opt.h"
#include "emit/emit.h"

namespace {

// Decompile one function and return the pseudocode text (or an error string).
qstring decompile_text(func_t *pfn) {
  ir::Function fn;
  qstring err;
  if (!mcore::lift_function(pfn, fn, err)) {
    qstring r("// decompile failed: ");
    r += err;
    return r;
  }
  // Recover variables before the optimizer rewrites/removes assignments, so
  // input (parameter) detection is accurate.
  vars::VarMap vm = vars::analyze(fn);
  opt::simplify(fn);
  std::string c = emit::emit_c(fn, vm);
  return qstring(c.c_str());
}

struct mcore_plugmod_t : public plugmod_t {
  mcore_plugmod_t() { msg("[mcore-decompiler] loaded\n"); }

  bool idaapi run(size_t /*arg*/) override {
    // Batch-friendly: decompile every function and print. A focused, cursor-based
    // action + custom viewer come in B8.
    size_t qty = get_func_qty();
    msg("[mcore-decompiler] run: %zu function(s)\n", qty);
    for (size_t i = 0; i < qty; ++i) {
      func_t *pfn = getn_func(i);
      // Unique markers so headless tests can extract pseudocode from the log.
      msg(">>>MCORE_FUNC %a\n%s<<<MCORE_END\n", pfn->start_ea,
          decompile_text(pfn).c_str());
    }
    return true;
  }
};

plugmod_t *idaapi init() {
  return new mcore_plugmod_t();
}

} // namespace

plugin_t PLUGIN = {
  IDP_INTERFACE_VERSION,
  PLUGIN_MULTI,                 // init() returns a plugmod_t; term/run are null
  init,
  nullptr,                      // term  (must be null for PLUGIN_MULTI)
  nullptr,                      // run   (must be null for PLUGIN_MULTI)
  "Decompile M-CORE functions to C pseudocode",
  "Self-contained M-CORE decompiler: insn_t -> IR -> C (no Hex-Rays).",
  "M-CORE Decompiler",
  "Ctrl-Shift-M",
};
