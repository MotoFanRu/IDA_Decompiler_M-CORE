// M-CORE decompiler plugin for IDA Pro 9 (Hex-Rays microcode backend).
//
// M0: loadable skeleton. It verifies the Hex-Rays decompiler is present and
// exposes a run() entry point. The actual microcode pipeline (create_empty_mba
// -> lift -> create_cfunc) is wired in M1 (the spike) and beyond.
#include <ida.hpp>
#include <idp.hpp>
#include <loader.hpp>
#include <kernwin.hpp>
#include <hexrays.hpp>

namespace {

struct mcore_plugmod_t : public plugmod_t {
  bool hexrays_ok = false;

  mcore_plugmod_t() {
    hexrays_ok = init_hexrays_plugin();
    msg("[mcore-decompiler] loaded (hex-rays decompiler %s)\n",
        hexrays_ok ? "available" : "NOT available");
  }

  ~mcore_plugmod_t() override { term_hexrays_plugin(); }

  bool idaapi run(size_t arg) override {
    if (!hexrays_ok) {
      warning("M-CORE decompiler: the Hex-Rays decompiler is not available.");
      return false;
    }
    // M1: this will drive create_empty_mba() -> lift -> create_cfunc() for the
    // function under the cursor. For now it only proves the entry point works.
    msg("[mcore-decompiler] run(arg=%llu) -- pipeline lands in M1\n",
        (unsigned long long)arg);
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
  "Decompile M-CORE functions via Hex-Rays microcode",
  "Lifts M-CORE instructions into Hex-Rays microcode to produce C pseudocode.",
  "M-CORE Decompiler",
  "Ctrl-Shift-M",
};
