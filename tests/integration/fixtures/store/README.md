# Fixture: store — `void f(int *p, int v){ *p = v; }`
| addr | bytes | instruction      |
|------|-------|------------------|
| 0x00 | 93 02 | `st.w r3,(r2,0)` | *r2 = r3
| 0x02 | 00 CF | `jmp r15` (rts)  | return
