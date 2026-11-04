/* SH-3 CPU state and reference interpreter API. GPL-2.0-or-later. */
#ifndef SH3_H
#define SH3_H
#include <stdint.h>

#define SR_T  0x00000001u
#define SR_S  0x00000002u
#define SR_IMASK 0x000000f0u
#define SR_Q  0x00000100u
#define SR_M  0x00000200u
#define SR_BL 0x10000000u
#define SR_RB 0x20000000u
#define SR_MD 0x40000000u

typedef struct SH3Bus {
    void *ctx;
    uint8_t  (*rb)(void *ctx, uint32_t a);
    uint16_t (*rw)(void *ctx, uint32_t a);
    uint32_t (*rl)(void *ctx, uint32_t a);
    void (*wb)(void *ctx, uint32_t a, uint8_t v);
    void (*ww)(void *ctx, uint32_t a, uint16_t v);
    void (*wl)(void *ctx, uint32_t a, uint32_t v);
    uint16_t (*fetch16)(void *ctx, uint32_t a);   /* opcode fetch without data timing (optional) */
} SH3Bus;

typedef struct SH3 {
    uint32_t r[16];      /* current bank in r[0..7] */
    uint32_t rbank[8];   /* the other bank */
    uint32_t sr, gbr, vbr, ssr, spc, mach, macl, pr, pc;
    /* interpreter control */
    uint32_t next_pc;    /* set by JUMP (no delay slot) */
    uint32_t slot_target;
    int has_slot;        /* set by DELAY */
    int sleeping;
    int trap;            /* TRAPA pending (imm + 1), handled by runtime */
    uint64_t insns;
    uint64_t cycles;     /* instruction-count clock (timing model later) */
    uint64_t event_at;
    /* timing hook, called before each executed instruction: returns extra cycles (base-1 + fetch penalty) */
    uint32_t (*fetch_hook)(void *ctx, uint32_t pc, uint16_t op);   /* generated code returns to the dispatcher once cycles >= event_at */
    SH3Bus bus;
} SH3;

void sh3_reset(SH3 *s);
void sh3_set_sr(SH3 *s, uint32_t v);          /* handles register bank switch */
void sh3_step(SH3 *s);                         /* one instruction (+ its delay slot) */
void sh3_interrupt(SH3 *s, uint32_t vector_offset, uint32_t intevt_unused);
void sh3_div1(SH3 *s, uint32_t *rn, uint32_t rm);
void sh3_macl(SH3 *s, int n, int m);
void sh3_macw(SH3 *s, int n, int m);
const char *sh3_mnemonic(uint16_t op);         /* NULL if illegal */
#endif
