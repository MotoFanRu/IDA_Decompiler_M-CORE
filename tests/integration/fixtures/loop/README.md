# Fixture: loop — `int f(int a, int b){ while (a < b) a = a + 1; return a; }`
| addr | bytes | instruction     |
|------|-------|-----------------|
| 0x00 | 0D 32 | `cmplt r2, r3`  | C = (a < b)
| 0x02 | E8 02 | `bf 0x08`       | exit loop if !C
| 0x04 | 20 02 | `addi r2, 1`    | a = a + 1
| 0x06 | F7 FC | `br 0x00`       | back edge
| 0x08 | 00 CF | `jmp r15` (rts) | return a
