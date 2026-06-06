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

#include <cctype>
#include <set>
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
  opt::split_ranges(fn);  // separate distinct values sharing a register
  vars::VarMap vm = vars::analyze(fn);
  opt::simplify(fn);
  opt::inline_locals(fn);
  std::string c = emit::emit_c(fn, vm);
  return qstring(c.c_str());
}

// Wrap a span in an IDA color tag: SCOLOR_ON <tag> text SCOLOR_OFF <tag>.
std::string col(char tag, const std::string &s) {
  return std::string(1, '\x01') + tag + s + '\x02' + tag;
}

// Syntax-highlight one pseudocode line with IDA color tags.
std::string colorize(const std::string &line) {
  static const std::set<std::string> kw = {
      "int", "void", "char", "short", "unsigned", "long", "return", "if", "else",
      "while", "do", "for", "break", "continue", "switch", "case", "default", "goto"};
  const char KEYWORD = '\x20', NUMBER = '\x0C', CMT = '\x04', CNAME = '\x25',
             LOCAL = '\x19', DNAME = '\x07';
  std::string out;
  size_t i = 0, n = line.size();
  while (i < n) {
    char c = line[i];
    if (c == '/' && i + 1 < n && line[i + 1] == '/') {  // comment to end of line
      out += col(CMT, line.substr(i));
      break;
    }
    if (std::isalpha((unsigned char)c) || c == '_') {   // identifier / keyword
      size_t j = i;
      while (j < n && (std::isalnum((unsigned char)line[j]) || line[j] == '_')) ++j;
      std::string id = line.substr(i, j - i);
      bool is_call = j < n && line[j] == '(';
      bool is_local = (id.size() >= 2 && (id[0] == 'a' || id[0] == 'v') &&
                       std::isdigit((unsigned char)id[1])) ||
                      id.rfind("var_", 0) == 0 || id == "cond";
      char tag = kw.count(id) ? KEYWORD : is_call ? CNAME : is_local ? LOCAL : DNAME;
      out += col(tag, id);
      i = j;
      continue;
    }
    if (std::isdigit((unsigned char)c)) {               // number (dec or 0x..)
      size_t j = i;
      if (c == '0' && i + 1 < n && (line[i + 1] == 'x' || line[i + 1] == 'X')) {
        j = i + 2;
        while (j < n && std::isxdigit((unsigned char)line[j])) ++j;
      } else {
        while (j < n && std::isdigit((unsigned char)line[j])) ++j;
      }
      out += col(NUMBER, line.substr(i, j - i));
      i = j;
      continue;
    }
    out += c;
    ++i;
  }
  return out;
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
    std::string ln = s.substr(start, nl == std::string::npos ? nl : nl - start);
    lines->push_back(simpleline_t(colorize(ln).c_str()));
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

const char *const kActionName = "mcore:decompile";

// Only our target processor; on supported processors (ARM, x86, ...) we must NOT
// touch F5 so the real Hex-Rays decompiler keeps it.
bool is_mcore() { return inf_get_procname() == "M*CORE"; }

// Bind F5 to our action only on M*CORE (taking it from the Hex-Rays "dummy"
// warning action shown for unsupported processors); release it otherwise.
void update_f5_binding() {
  if (is_mcore()) {
    update_action_shortcut("dummy_hexrays:warn", "");  // free F5 from the dummy
    update_action_shortcut(kActionName, "F5");
  } else {
    update_action_shortcut(kActionName, "");           // leave F5 to the real decompiler
  }
}

struct decompile_ah_t : public action_handler_t {
  int idaapi activate(action_activation_ctx_t *) override {
    if (!is_mcore()) return 0;
    func_t *pfn = get_func(get_screen_ea());
    if (pfn == nullptr) { warning("M-CORE: no function under the cursor."); return 0; }
    show_pseudocode(pfn);
    return 1;
  }
  action_state_t idaapi update(action_update_ctx_t *) override {
    return is_mcore() ? AST_ENABLE : AST_DISABLE;
  }
};
decompile_ah_t g_decompile_ah;

// Re-evaluate the F5 binding whenever a database finishes loading / the UI is ready
// (the processor is known by then).
struct ui_claim_t : public event_listener_t {
  ssize_t idaapi on_event(ssize_t code, va_list) override {
    if (code == ui_database_inited || code == ui_ready_to_run) update_f5_binding();
    return 0;
  }
};
ui_claim_t g_ui_claim;

struct mcore_plugmod_t : public plugmod_t {
  mcore_plugmod_t() {
    // Register without a default shortcut so registration never conflicts; F5 is
    // bound dynamically by update_f5_binding() only on M*CORE databases.
    register_action(ACTION_DESC_LITERAL(kActionName, "Decompile (M-CORE)",
                                        &g_decompile_ah, nullptr,
                                        "Decompile the current M-CORE function", -1));
    hook_event_listener(HT_UI, &g_ui_claim);
    update_f5_binding();
    msg("[mcore-decompiler] loaded%s\n", is_mcore() ? " (press F5 in a function)" : "");
  }
  ~mcore_plugmod_t() override {
    unhook_event_listener(HT_UI, &g_ui_claim);
    unregister_action(kActionName);
  }

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
  PLUGIN_MULTI | PLUGIN_FIX,     // load at startup so the F5 action is ready
  init,
  nullptr,                      // term  (must be null for PLUGIN_MULTI)
  nullptr,                      // run   (must be null for PLUGIN_MULTI)
  "Decompile M-CORE functions to C pseudocode",
  "Self-contained M-CORE decompiler: insn_t -> IR -> C (no Hex-Rays).",
  "M-CORE Decompiler",
  "Ctrl-Shift-M",
};
