# Headless helper: turn a flat M*CORE fixture into code+function(s) and run the
# decompiler plugin. Pseudocode is printed to the IDA message log between
# >>>MCORE_FUNC / <<<MCORE_END markers (see run_integration.sh).
#
# Assumes a single flat code region starting at min_ea (sufficient for the current
# fixtures). Multi-segment / data-aware fixtures come with later milestones.
import ida_auto, ida_bytes, ida_funcs, ida_ida, ida_loader, ida_ua, ida_pro, idc

ida_auto.auto_wait()

start = ida_ida.inf_get_min_ea()
end = ida_ida.inf_get_max_ea()

# Force the whole range to instructions.
ea = start
while ea < end:
    if ida_bytes.is_code(ida_bytes.get_flags(ea)):
        sz = idc.get_item_size(ea)
    else:
        sz = ida_ua.create_insn(ea)
    ea += sz if sz > 0 else 2

# Define a function at the entry if none exists yet.
if ida_funcs.get_func_qty() == 0:
    ida_funcs.add_func(start, end)
ida_auto.auto_wait()

# Decompile each function by address (arg != 0 prints the >>>MCORE_FUNC markers).
for i in range(ida_funcs.get_func_qty()):
    ida_loader.load_and_run_plugin("mcore_decompiler", ida_funcs.getn_func(i).start_ea)
ida_pro.qexit(0)
