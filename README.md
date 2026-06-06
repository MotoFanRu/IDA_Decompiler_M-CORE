# mcore_decompiler

A self-contained **decompiler for Motorola M·CORE** binaries, packaged as an
IDA Pro 9 plugin. It lifts the disassembly produced by the M·CORE processor
module into its own intermediate representation, optimises and structures it,
and emits **readable C pseudocode** with recovered local variables and
arguments — bound to **F5** inside any M·CORE database.

> Hex-Rays does not provide a decompiler back end for M·CORE, so there is no
> microcode target to plug into. This project takes the other route ("Path B"):
> a small, independent decompilation pipeline (IR + optimiser + control-flow
> structuring + C emitter), with no dependency on the Hex-Rays SDK.

```c
int BAP_AnyStateExit(int a1, int a2)
{
  int v3;

  v3 = a1;
  if (a2 == 0) {
    a2 = *(int *)(a1 + 28);
    if (a2 != 0) {
      UIS_Delete(a2);
      *(int *)(v3 + 28) = 0;
    }
  }
  return 0;
}
```

<sub>Verbatim output for a function from Motorola E1000 firmware — recovered
arguments, structured `if`s, and struct-field accesses.</sub>

## Why

M·CORE is a 16-bit fixed-width, big-endian RISC ISA Motorola used in embedded
systems and feature-phone firmware (e.g. the Motorola E1000). IDA Pro can
disassemble it with a community processor module, but cannot decompile it. This
plugin fills that gap with output aimed at **readability** rather than at being
a drop-in Hex-Rays equivalent.

## Features

- **Readable C** with named recovered variables (`a1..`, `v1..`, `var_<off>`),
  not raw registers.
- **Control-flow structuring**: `if/else`, `while`, `do/while`, `while (1)`,
  `break`/`continue`; a correct `goto` fallback for CFGs that don't structure.
- **Stack-frame recovery**: sp-relative slots become `var_<off>` locals;
  prologue/epilogue (sp adjust, `stm`/`ldm`, lr save/restore) is dropped;
  by-reference stack arguments render as `&var_<off>`.
- **Call-argument recovery** across basic blocks: arguments set in a predecessor
  block or on all arms of a branch are recovered; previous-call results are
  threaded into the next call (`p = alloc(); memset(p, …)`).
- **Optimisation**: constant/copy propagation and folding, the single M·CORE
  C bit resolved by reaching-definition analysis, live-range splitting
  (SSA-lite), liveness-based dead-store elimination, algebraic identities, and
  collapsing of branches a predecessor already decided.
- **Near-complete instruction coverage**; the rare control-register ops
  (`mfcr`/`mtcr`) are emitted as `__asm { … }` inserts rather than dropped.
- **Interactive**: F5 in a function prints to the Output window and opens a
  syntax-highlighted pseudocode viewer. The F5 binding is installed **only** on
  M·CORE databases, so it never shadows the real Hex-Rays F5 elsewhere.

## Requirements

- **IDA Pro 9.0** (64-bit, Linux) with the SDK headers and `libida.so`.
- The **M·CORE processor module** installed in IDA (`procs/`), so the database
  disassembles. See [THIRD_PARTY-NOTICES.md](THIRD_PARTY-NOTICES.md) — it is a
  third-party module referenced here as a git submodule and is **not**
  redistributed in this repository.
- A C++17 compiler and CMake ≥ 3.20.

The pipeline core (lifter/opt/vars/emit) is pure C++ with no IDA dependency and
is unit-tested offline; only the plugin shell and the lifter's enum headers
touch the SDK.

## Building

```sh
# 1. Clone with the processor-module submodule (needed for the enum headers)
git clone --recurse-submodules <your-fork-url> mcore_decompiler
cd mcore_decompiler
#   (or, if already cloned: git submodule update --init)

# 2. Point the build at your IDA install (ships the SDK headers + libida.so)
export IDA_DIR=/path/to/ida-pro-9.0      # or pass -DIDA_DIR=... to cmake

# 3. Configure and build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
# -> build/mcore_decompiler.so
```

`cmake/IdaSdk.cmake` resolves the SDK from `IDA_DIR` (env var or `-DIDA_DIR`).

### Installing

```sh
cp build/mcore_decompiler.so ~/.idapro/plugins/
```

### Optional: building the processor module from source

The plugin only needs the installed `procs/mcore*.so` at run time. If you want a
reproducible Linux build of the vendored module too, configure with
`-DBUILD_MCORE_PROC=ON` (produces a separate `.so`; not auto-installed).

## Usage

1. Open an M·CORE database in IDA 9 (the M·CORE processor module must be
   active — the title bar shows `M*CORE`).
2. Put the cursor in a function and press **F5** (or run the
   `Decompile (M-CORE)` action).
3. The pseudocode is printed to the **Output** window and shown in a custom
   viewer.

## How it works

The plugin reads IDA's decoded instructions and runs a fixed pipeline:

```
insn_t  ──lift──▶  IR
        recover_call_args     (cross-block / merge argument recovery)
        recover_stack         (sp slots → locals, drop prologue/epilogue)
        split_ranges          (live-range splitting / SSA-lite)
        vars::analyze         (name inputs, locals, stack slots)
        simplify              (const/copy prop, C-bit reaching, identities)
        fold_implied_branches (drop branches the predecessor already decided)
        inline_locals         (single-use inlining, dead-store elimination)
   ──emit──▶  structured C  (or a goto fallback)
```

The IR (`src/ir/ir.h`) is a CFG of basic blocks with a small expression/statement
language. See [docs/design.md](docs/design.md) for the design notes (in Russian).

## Tests

Offline unit tests cover the lifter, optimiser, variable recovery and emitter
end-to-end (no IDA needed); integration fixtures run the installed plugin
headlessly over hand-assembled M·CORE programs and diff against expected C.

```sh
# unit tests only (no IDA required)
cmake -S . -B build && cmake --build build -j"$(nproc)"
./build/unit_tests

# everything (unit + headless integration; needs IDA_DIR with idat)
./run_tests.sh
```

## Scope & limitations

This is a research-grade decompiler focused on readable output, not a verified
or complete one. Known limits:

- **No prototype/type recovery**: every value is `int`; return type is `int`,
  arguments are `a1..`. A directly forwarded, unmodified incoming argument
  (`g(a1)` with no setup instruction) can be dropped from a call.
- A minority of complex CFGs (nested loops, multi-block `do/while`) fall back to
  correct `goto` code rather than fully structured control flow.
- Jump tables (`jmpi`) are treated as tail calls — the M·CORE module exposes no
  switch info to recover them.
- Carry-shift and control-register ops that touch processor state are emitted as
  `__asm { … }`.

## Acknowledgements

Disassembly is provided by the third-party **M·CORE processor module for IDA
Pro** (https://github.com/MotoFanRu/M-CORE_IDA-Pro) — original work by
`rshade@hushmail.com` (2004–2005), ported to modern IDA by
[@usernameak](https://github.com/usernameak) and the MotoFan.Ru developers. See
[THIRD_PARTY-NOTICES.md](THIRD_PARTY-NOTICES.md).

## License

[MIT](LICENSE) for the code in this repository. The vendored M·CORE processor
module is referenced as a submodule and remains under its own (upstream) terms.
