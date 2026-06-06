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
#include <name.hpp>

#include <string>

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
  // Recover the stack frame first (sp slots -> locals, drop prologue/epilogue),
  // then recover variables (before the optimizer rewrites/removes assignments).
  opt::recover_stack(fn);
  vars::VarMap vm = vars::analyze(fn);
  opt::simplify(fn);
  opt::inline_locals(fn);
  std::string c = emit::emit_c(fn, vm);
  return qstring(c.c_str());
}

// Show a function's pseudocode: always to the Output window, plus a custom
// viewer window when running under the GUI.
void show_pseudocode(func_t *pfn) {
  qstring text = decompile_text(pfn);
  qstring fname;
  get_func_name(&fname, pfn->start_ea);

  msg("/* ---- %s (%a) ---- */\n%s\n", fname.c_str(), pfn->start_ea, text.c_str());

  // Custom viewer (leaks one strvec per open; acceptable for a tool).
  strvec_t *lines = new strvec_t;
  std::string s = text.c_str();
  for (size_t start = 0;;) {
    size_t nl = s.find('\n', start);
    lines->push_back(simpleline_t(s.substr(start, nl == std::string::npos ? nl : nl - start).c_str()));
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  qstring title;
  title.sprnt("Pseudocode: %s", fname.c_str());
  simpleline_place_t pmin(0);
  simpleline_place_t pmax((int)(lines->empty() ? 0 : lines->size() - 1));
  simpleline_place_t pcur(0);
  TWidget *w = create_custom_viewer(title.c_str(), &pmin, &pmax, &pcur, nullptr,
                                    lines, nullptr, nullptr);
  if (w != nullptr)
    display_widget(w, WOPN_DP_TAB | WOPN_RESTORE);
  else
    delete lines;  // no GUI (headless): nothing holds the buffer
}

struct decompile_ah_t : public action_handler_t {
  int idaapi activate(action_activation_ctx_t *) override {
    func_t *pfn = get_func(get_screen_ea());
    if (pfn == nullptr) { warning("M-CORE: no function under the cursor."); return 0; }
    show_pseudocode(pfn);
    return 1;
  }
  action_state_t idaapi update(action_update_ctx_t *) override { return AST_ENABLE_ALWAYS; }
};
decompile_ah_t g_decompile_ah;
const char *const kActionName = "mcore:decompile";

struct mcore_plugmod_t : public plugmod_t {
  mcore_plugmod_t() {
    register_action(ACTION_DESC_LITERAL(kActionName, "Decompile (M-CORE)",
                                        &g_decompile_ah, "F5",
                                        "Decompile the current M-CORE function", -1));
    msg("[mcore-decompiler] loaded (press F5 in a function)\n");
  }
  ~mcore_plugmod_t() override { unregister_action(kActionName); }

  bool idaapi run(size_t arg) override {
    // arg == SIZE_MAX: every function, with markers (eval/test harness).
    // arg != 0: the function at that address, with markers (harness, per-ea).
    // arg == 0: interactive — the function under the cursor (same as F5).
    auto dump = [](func_t *p) {
      msg(">>>MCORE_FUNC %a\n%s<<<MCORE_END\n", p->start_ea, decompile_text(p).c_str());
    };
    if (arg == (size_t)-1) {
      for (size_t i = 0, qty = get_func_qty(); i < qty; ++i) dump(getn_func(i));
      return true;
    }
    if (arg != 0) {
      if (func_t *pfn = get_func((ea_t)arg)) dump(pfn);
      return true;
    }
    func_t *pfn = get_func(get_screen_ea());
    if (pfn == nullptr) {
      msg("[mcore-decompiler] place the cursor inside a function (or press F5).\n");
      return false;
    }
    show_pseudocode(pfn);
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
