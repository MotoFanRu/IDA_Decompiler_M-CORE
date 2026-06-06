# Third-party notices

## M·CORE processor module for IDA Pro

This project builds **on top of** a third-party IDA Pro processor (disassembler)
module for the Motorola M·CORE architecture. It is **not** redistributed inside
this repository — it is referenced as a git submodule and fetched from its
upstream:

- Submodule path: `third_party/mcore-proc`
- Upstream: https://github.com/MotoFanRu/M-CORE_IDA-Pro

How this repository uses it:

- At **build time**, the lifter includes the module's enumeration headers
  (`ins.hpp`, `mcore.hpp`) to map IDA's decoded `insn_t.itype` / register numbers
  to symbolic `mcore_*` names. No module source is copied into this tree.
- At **run time**, the *installed* M·CORE processor module (already present in
  your IDA `procs/` directory) performs the disassembly; this plugin only reads
  the resulting `insn_t` structures.

Attribution, as stated by the upstream project and the original source headers:

- Original "IDA MCORE Plugin", Copyright (c) 2004–2005 `rshade@hushmail.com`.
- Ported and maintained for modern IDA Pro (8.3 / 9.0) by
  [@usernameak](https://github.com/usernameak) and the MotoFan.Ru developers
  (erithion, yakk, GanjaFuzz, Chik_v, theCore, and others).

The upstream module does not ship an explicit open-source license file. It is
used here under its own (upstream) terms; this repository claims no rights over
it. If you are the rights holder and want the attribution or usage adjusted,
please open an issue.
