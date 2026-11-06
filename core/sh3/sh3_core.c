/* SH-3 reference interpreter: helpers and step loop. GPL-2.0-or-later.
 * Instruction handlers are generated from isa.py into sh3_interp.inc (build step). */
#include "sh3.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define S (s)
#include "sh3_ops.h"
#include "sh3_cycles.h"

void sh3_set_sr(SH3 *s, uint32_t v)
{
    v &= 0x700083f3u;
    uint32_t oldrb = (s->sr & SR_MD) && (s->sr & SR_RB);
    uint32_t newrb = (v & SR_MD) && (v & SR_RB);
    if (oldrb != newrb) {
        for (int i = 0; i < 8; i++) { uint32_t t = s->r[i]; s->r[i] = s->rbank[i]; s->rbank[i] = t; }
    }
    s->sr = v;
}

void sh3_reset(SH3 *s)
{
    SH3Bus bus = s->bus; uint32_t (*hook)(void *, uint32_t, uint16_t) = s->fetch_hook; int acct = s->acct;
    memset(s, 0, sizeof *s);
    s->bus = bus; s->fetch_hook = hook; s->acct = acct;
    s->pc = 0xA0000000u;
    s->vbr = 0;
    s->sr = 0x700000F0u; /* MD=1, RB=1, BL=1, IMASK=15 (bank already "1": registers start in r[]) */
}

void sh3_div1(SH3 *s, uint32_t *rn, uint32_t rm)
{
    uint32_t old_q = (s->sr >> 8) & 1, M = (s->sr >> 9) & 1, Q = (*rn >> 31) & 1, tmp0, tmp1;
    *rn = (*rn << 1) | (s->sr & 1);
    if (old_q == M) { tmp0 = *rn; *rn -= rm; tmp1 = *rn > tmp0; }
    else            { tmp0 = *rn; *rn += rm; tmp1 = *rn < tmp0; }
    /* Q update per SH programming manual: Q = Q ^ M ^ tmp1 ^ old_q ... expanded: */
    if (old_q == 0) Q = (M == 0) ? (Q ? !tmp1 : tmp1) : (Q ? tmp1 : !tmp1);
    else            Q = (M == 0) ? (Q ? !tmp1 : tmp1) : (Q ? tmp1 : !tmp1);
    s->sr = (s->sr & ~(SR_Q | SR_T)) | (Q << 8) | (Q == M);
}

void sh3_macl(SH3 *s, int n, int m)
{
    int32_t a = (int32_t)RL(s->r[n]); s->r[n] += 4;
    int32_t b = (int32_t)RL(s->r[m]); s->r[m] += 4;
    int64_t acc = (int64_t)(((uint64_t)s->mach << 32) | s->macl);
    acc += (int64_t)a * b;
    if (s->sr & SR_S) {
        if (acc > 0x00007fffffffffffLL) acc = 0x00007fffffffffffLL;
        if (acc < -0x0000800000000000LL) acc = -0x0000800000000000LL;
    }
    s->mach = (uint32_t)((uint64_t)acc >> 32); s->macl = (uint32_t)acc;
}

void sh3_macw(SH3 *s, int n, int m)
{
    int32_t a = (int16_t)RW(s->r[n]); s->r[n] += 2;
    int32_t b = (int16_t)RW(s->r[m]); s->r[m] += 2;
    int32_t p = a * b;
    if (s->sr & SR_S) {
        int64_t r = (int64_t)(int32_t)s->macl + p;
        if (r > 0x7fffffffLL) { r = 0x7fffffffLL; s->mach |= 1; }
        if (r < -0x80000000LL) { r = -0x80000000LL; s->mach |= 1; }
        s->macl = (uint32_t)r;
    } else {
        int64_t acc = (int64_t)(((uint64_t)s->mach << 32) | s->macl) + p;
        s->mach = (uint32_t)((uint64_t)acc >> 32); s->macl = (uint32_t)acc;
    }
}

static int ops_ready;
static int ops_ready_ext(void) { return ops_ready; }
#include "sh3_interp.inc"   /* static void (*sh3_ops[65536])(SH3*, uint16_t); sh3_init_ops(); */

void sh3_step(SH3 *s)
{
    if (!ops_ready) { sh3_init_ops(); ops_ready = 1; }
    uint32_t pc = s->pc;
    uint16_t op = s->bus.fetch16 ? s->bus.fetch16(s->bus.ctx, pc) : RW(pc);
    if (s->fetch_hook) s->cycles += s->fetch_hook(s->bus.ctx, pc, op);
    s->next_pc = pc + 2;
    s->has_slot = 0;
    sh3_ops[op](s, op);
    s->insns++;
    int cls = s->acct ? sh3_acct_class(op) : 0;
    if (s->acct) {
        s->acc += sh3_base_cycles(op);
        if ((op & 0xF0FF) == 0x400E || (op & 0xF0FF) == 0x4007) s->acc = 0;   /* ldc Rm,SR / ldc.l @Rm+,SR: MAME checkints drop */
        else if (cls == ACCT_FLUSH) { s->cycles += s->acc; s->acc = 0; }
    }
    else s->cycles++;
    if (cls == ACCT_COND && !s->has_slot && s->next_pc == pc + 2) s->acc = 0;          /* not taken: dropped */
    if (s->has_slot) {
        uint32_t target = s->slot_target;
        s->pc = pc + 2;
        uint16_t op2 = s->bus.fetch16 ? s->bus.fetch16(s->bus.ctx, pc + 2) : RW(pc + 2);
        if (s->fetch_hook) s->cycles += s->fetch_hook(s->bus.ctx, pc + 2, op2);
        s->has_slot = 0;
        s->next_pc = pc + 4;
        sh3_ops[op2](s, op2);
        s->insns++;
        if (s->acct) { s->acc += sh3_base_cycles(op2); if (op == 0x002B) s->acc = 0; else s->cycles += s->acc; s->acc = 0; }   /* taken delayed branch (rte: MAME checkints drop) */
        else s->cycles++;
        s->pc = target;
    } else {
        if (s->acct && (cls == ACCT_END || (cls == ACCT_COND && s->next_pc != pc + 2))) { s->cycles += s->acc; s->acc = 0; }
        s->pc = s->next_pc;
    }
}

void sh3_interrupt(SH3 *s, uint32_t vector_offset, uint32_t unused)
{
    (void)unused;
    s->spc = s->pc;
    s->ssr = s->sr;
    sh3_set_sr(s, s->sr | SR_MD | SR_RB | SR_BL);
    s->pc = s->vbr + vector_offset;
    s->sleeping = 0;
}
