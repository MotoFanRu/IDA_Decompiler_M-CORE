# Fixture: frame — prologue/epilogue + stack slot
| addr | bytes | instruction       | role |
|------|-------|-------------------|------|
| 0x00 | 24 70 | `subi r0, 8`      | prologue (sp -= 8) — hidden
| 0x02 | 9F 10 | `st r15,(r0,4)`   | save lr — hidden
| 0x04 | 92 00 | `st r2,(r0,0)`    | a1 -> stack slot 0
| 0x06 | 83 00 | `ld r3,(r0,0)`    | dead (rts returns r2) — eliminated
| 0x08 | 8F 10 | `ld r15,(r0,4)`   | restore lr — hidden
| 0x0A | 20 70 | `addi r0, 8`      | epilogue (sp += 8) — hidden
| 0x0C | 00 CF | `jmp r15` (rts)   | return r2
