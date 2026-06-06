# Fixture: rsubi — `int f(int a){ return 5 - a; }`
| addr | bytes | instruction    |
|------|-------|----------------|
| 0x00 | 28 52 | `rsubi r2, 5`  | r2 = 5 - r2
| 0x02 | 00 CF | `jmp r15`(rts) | return r2
