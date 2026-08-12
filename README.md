# mcore_decompiler

A self-contained **decompiler for Motorola M·CORE** binaries, packaged as an
IDA Pro 9.4 plugin. It lifts the disassembly produced by IDA's built-in MCORE
processor into its own intermediate representation, optimises and structures it,
and emits **readable C pseudocode** with recovered local variables and
arguments — bound to **F5** inside any M·CORE database.

> Hex-Rays does not provide a decompiler back end for M·CORE, so there is no
> microcode target to plug into. This project takes the other route ("Path B"):
> a small, independent decompilation pipeline (IR + optimiser + control-flow
> structuring + C emitter), with no dependency on Hex-Rays decompiler APIs.

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
systems and feature-phone firmware (e.g. the Motorola E1000). IDA Pro 9.4 can
disassemble it with its built-in processor module, but cannot decompile it. This
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

- **IDA Pro 9.4** (64-bit, Linux), including its built-in `procs/mcore.so`.
- The official [IDA SDK](https://github.com/HexRaysSA/ida-sdk) checkout at tag
  `v9.4.0-release`.
- A C++17 compiler and CMake ≥ 3.25.

The IR, optimization, variable-analysis, and emission stages are pure C++ and
unit-tested offline; only the plugin shell and instruction lifter touch the SDK.

## Building

```sh
# 1. Clone this project and the matching official SDK
git clone --depth=1 --recurse-submodules --shallow-submodules https://github.com/Siesta/MCORE-Decompiler
cd MCORE-Decompiler

# 2. Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# -> build/mcore_decompiler.dll
# -> build/mcore_decompiler.so
# -> build/mcore_decompiler.dylib
```

`IDASDK` may point either at the git checkout root or its `src/` directory.

### Installing

```sh
cp build/mcore_decompiler.dll IDA_Pro_ROOT/plugins/
cp build/mcore_decompiler.so ~/.idapro/plugins/
cp build/mcore_decompiler.dylib ~/.idapro/plugins/
```

## Usage

1. Open an M·CORE database in IDA 9.4 with the built-in processor active; the
   processor name is `MCORE` (without an asterisk).
2. Put the cursor in a function and press **F5** (or run the
   `Decompile (M-CORE)` action).
3. The pseudocode is printed to the **Output** window and shown in a custom
   viewer.

Databases created with the former third-party processor are stored as `M*CORE`
and cannot switch processor modules in place. This plugin keeps compatibility
with those databases when that processor is already installed, but all new
databases and integration tests use IDA 9.4's built-in `MCORE` module.

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

Offline unit tests cover the IR pipeline, optimiser, variable recovery and
emitter (no IDA runtime needed); integration fixtures run the plugin with IDA
9.4's built-in MCORE module and diff the output against expected C.

```sh
# unit tests (configuration still uses the official SDK)
cmake -S . -B build && cmake --build build -j"$(nproc)"
./build/unit_tests

# everything (unit + headless integration)
export IDA_DIR=/path/to/ida-pro-9.4
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

Disassembly is provided by IDA Pro 9.4's built-in MCORE processor. Earlier
versions of this project used the community
[M-CORE_IDA-Pro](https://github.com/MotoFanRu/M-CORE_IDA-Pro) module; thanks to
its original and modern maintainers for enabling the initial implementation.

## License

[MIT](LICENSE) for the code in this repository. IDA Pro and its processor
modules remain proprietary Hex-Rays software; the separately downloaded IDA SDK
is distributed under its own license.
