# Fixture: bgeni — `int f(void){ return 1 << 8; }`
| addr | bytes | instruction   | n=(word>>4)&0x1f |
|------|-------|---------------|------------------|
| 0x00 | 32 82 | `bgeni r2, 8` | r2 = 1<<8 = 0x100 |
| 0x02 | 00 CF | `jmp r15`     | return r2         |
