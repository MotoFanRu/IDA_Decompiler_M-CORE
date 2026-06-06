# Fixture: addi5 — `int f(int a){ return a + 5; }`
| addr | bytes | instruction  |
|------|-------|--------------|
| 0x00 | 20 42 | `addi r2, 5` | (r2 = r2 + 5)
| 0x02 | 00 CF | `jmp r15`    | (rts)
