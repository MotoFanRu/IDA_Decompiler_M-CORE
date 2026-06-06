# Fixture: return42

Smallest end-to-end fixture for the M1 spike.

`prog.bin` (big-endian M-CORE machine code, hand-encoded):

| addr | bytes  | instruction   |
|------|--------|---------------|
| 0x00 | 62 A2  | `movi r2, 42` |
| 0x02 | 00 CF  | `jmp r15`     | (== `rts`)

C equivalent (M-CORE ABI returns in r2):

```c
int f(void) { return 42; }
```

Load with the custom processor module: `idat -A -p'M*CORE' prog.bin`
(disassembly oracle for crafting/validating fixtures).

**Expected decompiler output (goal of M1):** `return 42;`
