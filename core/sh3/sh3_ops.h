/* Macros used by instruction bodies in isa.py. The including file defines S (pointer to SH3).
 * Shared by the interpreter and (later) the generated recompiled code. GPL-2.0-or-later. */
#ifndef SH3_OPS_H
#define SH3_OPS_H
#define R0   (S->r[0])
#define Rn   (S->r[n])
#define Rm   (S->r[m])
#define BANK(i) (S->rbank[(i)])
#define SR   (S->sr)
#define GBR  (S->gbr)
#define VBR  (S->vbr)
#define SSR  (S->ssr)
#define SPC  (S->spc)
#define MACH (S->mach)
#define MACL (S->macl)
#define PR   (S->pr)
#define T    (S->sr & 1u)
#define SET_T(x) (S->sr = (S->sr & ~1u) | ((x) ? 1u : 0u))
#define set_sr(v) sh3_set_sr(S, (v))
#define RB(a) ((uint32_t)S->bus.rb(S->bus.ctx, (a)))
#define RW(a) ((uint32_t)S->bus.rw(S->bus.ctx, (a)))
#define RL(a) (S->bus.rl(S->bus.ctx, (a)))
#define WB(a, v) S->bus.wb(S->bus.ctx, (a), (uint8_t)(v))
#define WW(a, v) S->bus.ww(S->bus.ctx, (a), (uint16_t)(v))
#define WL(a, v) S->bus.wl(S->bus.ctx, (a), (uint32_t)(v))
/* PC-relative literal loads: MAME's SH DRC (without SH2DRC_STRICT_PCREL, as used by cv1k speedups)
 * collapses these to immediates, so they make no timed memory access. Use the untimed fetch path. */
#define RW_PCREL(a) ((uint32_t)(S->bus.fetch16 ? S->bus.fetch16(S->bus.ctx, (a)) : S->bus.rw(S->bus.ctx, (a))))
#define RL_PCREL(a) (S->bus.fetch16 ? ((uint32_t)S->bus.fetch16(S->bus.ctx, (a)) << 16 | S->bus.fetch16(S->bus.ctx, (a) + 2)) : S->bus.rl(S->bus.ctx, (a)))
#endif
