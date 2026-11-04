/* SH-3 base cycles and MAME-DRC cycle accounting classes (MAME 0.289 sh_fe.cpp / sh.cpp / sh4.cpp,
 * BSD-3-Clause). GPL-2.0-or-later. */
#ifndef SH3_CYCLES_H
#define SH3_CYCLES_H
#include <stdint.h>
static inline uint32_t sh3_base_cycles(uint16_t op)
{
    switch (op >> 12) {
    case 0x0:
        switch (op & 0xFF) { case 0x0B: return 2; case 0x1B: return 3; case 0x2B: return 4; case 0x23: return 2; }
        if ((op & 0xF) == 0x7) return 2;
        if ((op & 0xF) == 0xF) return 3;
        return 1;
    case 0x2: return ((op & 0xF) == 0xE || (op & 0xF) == 0xF) ? 2 : 1;
    case 0x3: return ((op & 0xF) == 0x5 || (op & 0xF) == 0xD) ? 2 : 1;
    case 0x4:
        if ((op & 0xF) == 0xF) return 3;
        switch (op & 0xFF) { case 0x03: return 2; case 0x07: return 3; case 0x1B: return 4; }
        return 1;
    case 0x8: { uint32_t s = (op >> 8) & 0xF; if (s == 0x9 || s == 0xB) return 3; if (s == 0xD || s == 0xF) return 2; return 1; }
    case 0xA: case 0xB: return 2;
    case 0xC: return ((op >> 8) & 0xF) == 0x3 ? 8 : 1;
    default: return 1;
    }
}
/* Accounting class: 0 none, 1 flush (unless in a delay slot), 2 charge at end (unconditional
 * control flow, incl. its slot), 3 conditional branch (charge if taken, DROP if not taken). */
enum { ACCT_NONE, ACCT_FLUSH, ACCT_END, ACCT_COND };
static inline int sh3_acct_class(uint16_t op)
{
    uint32_t g = op >> 12, lo = op & 0xF, lo8 = op & 0xFF;
    switch (g) {
    case 0x0:
        if (lo == 0x4 || lo == 0x5 || lo == 0x6 || lo == 0xC || lo == 0xD || lo == 0xE) return ACCT_FLUSH;  /* R0-indexed mov */
        if (lo8 == 0x0B || lo8 == 0x2B || lo8 == 0x1B || lo8 == 0x23 || lo8 == 0x03) return ACCT_END;        /* rts rte sleep braf bsrf */
        return ACCT_NONE;
    case 0x1: case 0x5: case 0x9: case 0xD: return ACCT_FLUSH;                                         /* disp / PC-rel mov */
    case 0x2: return lo <= 0x2 || (lo >= 0x4 && lo <= 0x6) ? ACCT_FLUSH : ACCT_NONE;                  /* mov Rm,@Rn / @-Rn */
    case 0x4:
        switch (lo8) {
        case 0x02: case 0x12: case 0x22: case 0x03: case 0x13: case 0x23:                                  /* sts.l/stc.l SR GBR VBR */
        case 0x06: case 0x16: case 0x26: case 0x07: case 0x17: case 0x27: case 0x1B: return ACCT_FLUSH;    /* lds.l/ldc.l, tas.b */
        case 0x0B: case 0x2B: return ACCT_END;                                                              /* jsr jmp */
        }
        return ACCT_NONE;
    case 0x6: return lo <= 0x2 || (lo >= 0x4 && lo <= 0x6) ? ACCT_FLUSH : ACCT_NONE;                  /* mov @Rm / @Rm+ */
    case 0x8: {
        uint32_t s = (op >> 8) & 0xF;
        if (s == 0x0 || s == 0x1 || s == 0x4 || s == 0x5) return ACCT_FLUSH;                           /* mov.b/w R0 disp */
        if (s == 0x9 || s == 0xB || s == 0xD || s == 0xF) return ACCT_COND;
        return ACCT_NONE;
    }
    case 0xA: case 0xB: return ACCT_END;
    case 0xC: { uint32_t s = (op >> 8) & 0xF; if (s <= 0x2 || (s >= 0x4 && s <= 0x6)) return ACCT_FLUSH; if (s == 0x3) return ACCT_END; return ACCT_NONE; }
    default: return ACCT_NONE;
    }
}
#endif
