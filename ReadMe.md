# M·CORE Decompiler Plugin for IDA Pro

A self-contained decompiler for [Motorola M·CORE](https://en.wikipedia.org/wiki/M%C2%B7CORE) architecture, packaged as an IDA Pro 9.4+ plugin. It lifts the disassembly produced by IDA's built-in M-CORE processor into its own intermediate representation, optimizes and structures it, and emits **readable C pseudocode** with recovered local variables and arguments. The decompiler is bound to the <kbd>F5</kbd> key inside any M-CORE (old and new) databases.

// IMG

Hex-Rays does not provide a decompiler backend for M-CORE, so there is no microcode target to plug into. This project takes an alternative route: a small, independent decompilation pipeline (IR + optimizer + control-flow + structuring + C emitter) with no dependency on Hex-Rays decompiler APIs. Arguments, structured `if` statements, and struct-field accesses can still be recovered.

## Download

You can find ready-to-install libraries on the GitHub Actions page:

**[https://github.com/MotoFanRu/IDA_Decompiler_M-CORE/actions](https://github.com/MotoFanRu/IDA_Decompiler_M-CORE/actions)**

## Build

```sh
git clone --depth=1 --recurse-submodules --shallow-submodules https://github.com/MotoFanRu/IDA_Decompiler_M-CORE

cd IDA_Decompiler_M-CORE

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# -> build/mcore_decompiler.dll
# -> build/mcore_decompiler.so
# -> build/mcore_decompiler.dylib
```

## Install

Place `mcore_decompiler.{dll,so,dylib}` file into the `plugins` directory of your root IDA Pro installation.

## Usage

Select a function and press <kbd>F5</kbd> to decompile it.

## Additional Information

* [ReadMe.ai.md](ReadMe.ai.md)
* https://github.com/MotoFanRu/IDA_Module_M-CORE
* https://docs.hex-rays.com/release-notes/9_4
