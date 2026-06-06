# Fixture: load — `int f(int *p){ return *p; }`
| addr | bytes | instruction      |
|------|-------|------------------|
| 0x00 | 82 02 | `ld.w r2,(r2,0)` | r2 = *r2
| 0x02 | 00 CF | `jmp r15` (rts)  | return r2
