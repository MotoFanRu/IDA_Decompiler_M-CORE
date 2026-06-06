# Fixture: add — `int f(int a, int b){ return a + b; }`
| addr | bytes | instruction   |
|------|-------|---------------|
| 0x00 | 1C 32 | `addu r2, r3` | (r2 = r2 + r3)
| 0x02 | 00 CF | `jmp r15`     | (rts)
