# Fixture: max — `int f(int a, int b){ if (a < b) a = b; return a; }`
| addr | bytes | instruction      |
|------|-------|------------------|
| 0x00 | 0D 32 | `cmplt r2, r3`   | C = (a < b)
| 0x02 | E8 01 | `bf 0x06`        | if !C skip the mov
| 0x04 | 12 32 | `mov r2, r3`     | a = b
| 0x06 | 00 CF | `jmp r15` (rts)  | return a
