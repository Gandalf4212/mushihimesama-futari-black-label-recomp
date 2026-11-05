/* CV1000 machine implementation. See cv1k.h for credits. GPL-2.0-or-later. */
#include "cv1k.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define P(a) ((a) & 0x1FFFFFFFu)
#define RAM_BASE 0x0C000000u
#define PAGE 2112u

/* ---------------- NAND (Samsung K9F1G08U0M: 2048+64 byte pages, 2 col + 2 row address cycles) ---- */
enum { N_INIT, N_READ, N_PROG, N_ERASE, N_STATUS, N_ID, N_RDOUT, N_RDIN };
static const uint8_t nand_id[5] = {0xEC, 0xF1, 0x00, 0x15, 0x00};

static void nand_cmd(Nand *n, uint8_t c)
{
    switch (c) {
    case 0xFF: n->mode = N_INIT; n->status = 0xC0; break;
    case 0x00: n->mode = N_READ; n->addr_ptr = 0; break;
    case 0x30: break;                                   /* read confirm: data ready immediately */
    case 0x05: n->mode = N_RDOUT; n->addr_ptr = 0; break;
    case 0xE0: n->mode = N_READ; break;
    case 0x80: n->mode = N_PROG; n->addr_ptr = 0; memset(n->pagereg, 0xFF, PAGE); break;
    case 0x85: n->mode = N_RDIN; n->addr_ptr = 0; break;
    case 0x10: case 0x15:
        if (n->mode == N_PROG || n->mode == N_RDIN) {
            uint32_t o = n->row * PAGE;
            if (o + PAGE <= n->size) for (unsigned i = 0; i < PAGE; i++) n->data[o + i] &= n->pagereg[i];
            n->dirty = 1;
        }
        n->mode = N_INIT; n->status = 0xC0; break;
    case 0x60: n->mode = N_ERASE; n->addr_ptr = 0; n->row = 0; break;
    case 0xD0:
        if (n->mode == N_ERASE) {
            uint32_t o = (n->row & ~63u) * PAGE;
            if (o + 64 * PAGE <= n->size) memset(n->data + o, 0xFF, 64 * PAGE);
            n->dirty = 1;
        }
        n->mode = N_INIT; n->status = 0xC0; break;
    case 0x70: n->mode = N_STATUS; break;
    case 0x90: n->mode = N_ID; n->addr_ptr = 0; n->col = 0; break;
    default: break;
    }
}

static void nand_addr(Nand *n, uint8_t v)
{
    int p = n->addr_ptr++;
    switch (n->mode) {
    case N_READ: case N_PROG:
        if (p == 0) n->row = 0;
        if (p < 2) n->col = (n->col & ~(0xFFu << (p * 8))) | ((uint32_t)v << (p * 8));
        else if (p < 4) n->row = (n->row & ~(0xFFu << ((p - 2) * 8))) | ((uint32_t)v << ((p - 2) * 8));
        break;
    case N_ERASE:
        if (p < 2) n->row = (n->row & ~(0xFFu << (p * 8))) | ((uint32_t)v << (p * 8));
        break;
    case N_RDOUT: case N_RDIN:
        if (p < 2) n->col = (n->col & ~(0xFFu << (p * 8))) | ((uint32_t)v << (p * 8));
        break;
    case N_ID: if (p == 0) n->col = v; break;
    default: break;
    }
}

static uint8_t nand_read(Nand *n)
{
    switch (n->mode) {
    case N_READ: case N_RDOUT: {
        uint8_t r = 0xFF;
        if (n->col < PAGE && n->row * PAGE + n->col < n->size) r = n->data[n->row * PAGE + n->col];
        n->col++; n->bytes_read++;
        return r;
    }
    case N_STATUS: return n->status;
    case N_ID: { uint8_t r = n->col < 5 ? nand_id[n->col] : 0; n->col++; return r; }
    default: return 0;
    }
}

static void nand_write(Nand *n, uint8_t v)
{
    if ((n->mode == N_PROG || n->mode == N_RDIN) && n->col < PAGE) n->pagereg[n->col++] = v;
}

/* ---------------- RTC9701 serial RTC + 256x16 EEPROM (bit-banged: b0 data, b1 clock, b2 /CS) ---- */
enum { R_CMD, R_WEN, R_RTCW, R_RTCR, R_EEW, R_EER };

static void rtc_cs(Rtc9701 *r, int active)
{
    r->cs = active;
    if (!active) { r->state = R_CMD; r->pos = 0; r->cmd = 0; r->addr = 0; r->data = 0; r->dpos = 0; }
}

static void rtc_clock_rise(Rtc9701 *r)
{
    int b = r->latch & 1;
    switch (r->state) {
    case R_CMD:
        r->cmd = (r->cmd << 1) | b;
        if (++r->pos == 4) {
            r->pos = 0; r->addr = 0; r->data = 0; r->dpos = 0;
            switch (r->cmd) {
            case 0x0: r->state = R_RTCW; break;
            case 0x2: r->state = R_EEW; break;
            case 0x6: r->state = R_WEN; break;
            case 0x8: r->state = R_RTCR; break;
            case 0xA: r->state = R_EER; break;
            default: break;
            }
            r->cmd = 0;
        }
        break;
    case R_WEN:
        if (++r->pos == 12) { r->pos = 0; r->state = R_CMD; }
        break;
    case R_RTCW:
        r->pos++;
        if (r->pos <= 4) r->addr = (r->addr << 1) | b; else r->data = (r->data << 1) | b;
        if (r->pos == 12) { r->rtc[r->addr & 7] = r->data; r->pos = 0; r->state = R_CMD; }
        break;
    case R_RTCR:
        r->pos++;
        if (r->pos <= 4) { r->addr = (r->addr << 1) | b; if (r->pos == 4) { r->data = (r->addr & 7) == 7 ? 0x20 : r->rtc[r->addr & 7]; r->dpos = 8; } }
        else r->dpos--;
        if (r->pos == 12) { r->pos = 0; r->state = R_CMD; }
        break;
    case R_EER:
        r->pos++;
        if (r->pos <= 12) { r->addr = (r->addr << 1) | b; if (r->pos == 12) { r->data = r->eeprom[(r->addr >> 1) & 0xFF]; r->dpos = 16; } }
        else r->dpos--;
        if (r->pos == 28) r->pos = 0;
        break;
    case R_EEW:
        r->pos++;
        if (r->pos <= 12) r->addr = (r->addr << 1) | b; else r->data = (r->data << 1) | b;
        if (r->pos == 28) { r->eeprom[(r->addr >> 1) & 0xFF] = r->data; r->dirty = 1; r->pos = 0; r->state = R_CMD; }
        break;
    }
}

static int rtc_read_bit(Rtc9701 *r)
{
    if ((r->state == R_RTCR || r->state == R_EER) && r->dpos > 0) return (r->data >> (r->dpos - 1)) & 1;
    return 0;
}

static void rtc_write(Rtc9701 *r, uint8_t v)
{
    r->latch = v & 1;
    int cs = (v >> 2) & 1;                 /* active when bit2 = 1 (line is active low, inverted by board) */
    if (cs != r->cs) rtc_cs(r, cs);
    int clk = (v >> 1) & 1;
    if (r->cs && clk && !r->clk) rtc_clock_rise(r);
    r->clk = clk;
}

/* ---------------- blitter busy time (model per MAME cv1k_v.cpp, BSD-3, David Haywood, Luca Elia,
 * MetalliC; based on buffi's CV1000 blitter research). On every kick the display list is walked and
 * its duration estimated from VRAM/SRAM clock counts; the ready bit (+0x10) reads 0 until then. ---- */
enum { BL_VRAM_CLK_NS = 13, BL_SRAM_CLK_NS = 20, BL_HLINE_PERIOD_NS = 63600, BL_HLINE_DURATION_NS = 2160,
       BL_DRAW_OP_BYTES = 20, BL_CLIP_OP_BYTES = 2, BL_CLIP_MARGIN = 32, BL_CHUNK_BYTES = 64, BL_CHUNK_NS = 700 };
static uint16_t bl_word(CV1K *m, uint32_t *a) { uint32_t o = *a & (m->ram_size - 1); *a += 2; return (uint16_t)(m->ram[o] << 8 | m->ram[o + 1]); }
static void bl_idle(Blitter *b, uint64_t *ns, int bytes)
{
    b->idle_op_bytes += bytes;
    if (b->idle_op_bytes >= BL_CHUNK_BYTES) { b->idle_op_bytes -= BL_CHUNK_BYTES; *ns += BL_CHUNK_NS; }
}
static uint16_t bl_vram_rows(uint16_t sx, uint16_t sy, uint16_t dx, uint16_t dy)
{
    int x_rows = 0, rows = 0;
    for (int xp = dx; xp > 0; xp -= 32) { x_rows++; if (((sx & 31) + (xp < 32 ? xp : 32)) > 32) x_rows++; }
    for (int yp = dy; yp > 0; yp -= 32) { rows += x_rows; if (((sy & 31) + (yp < 32 ? yp : 32)) > 32) rows += x_rows; }
    return (uint16_t)rows;
}
static uint64_t blit_duration_ns(CV1K *m)
{
    Blitter *b = &m->blit;
    uint32_t a = b->gfx_addr & 0x1FFFFFFFu;
    int cx = (int)b->clip_x, cy = (int)b->clip_y;
    int min_x = cx - BL_CLIP_MARGIN, max_x = cx + 320 - 1 + BL_CLIP_MARGIN, min_y = cy - BL_CLIP_MARGIN, max_y = cy + 240 - 1 + BL_CLIP_MARGIN;
    uint64_t ns = 0;
    for (int guard = 0; guard < 1 << 20; guard++) {
        uint16_t op = bl_word(m, &a);
        switch (op & 0xF000) {
        case 0x0000: case 0xF000: goto done;
        case 0xC000:
            if (bl_word(m, &a)) { min_x = cx - BL_CLIP_MARGIN; max_x = cx + 320 - 1 + BL_CLIP_MARGIN; min_y = cy - BL_CLIP_MARGIN; max_y = cy + 240 - 1 + BL_CLIP_MARGIN; }
            else { min_x = 0; max_x = 0x2000 - 1; min_y = 0; max_y = 0x1000 - 1; }
            bl_idle(b, &ns, BL_CLIP_OP_BYTES);
            break;
        case 0x2000: {                                       /* upload: header + pixels over the SRAM bus */
            a += 10;
            uint32_t w = (bl_word(m, &a) & 0x1FFF) + 1, h = (bl_word(m, &a) & 0x0FFF) + 1;
            a += w * h * 2;
            ns += (uint64_t)((16 + w * h * 2) / 4) * BL_SRAM_CLK_NS;
            b->idle_op_bytes = 0;
            break; }
        case 0x1000: {                                       /* draw */
            a += 2;
            uint16_t sx = bl_word(m, &a), sy = bl_word(m, &a), dx0 = bl_word(m, &a), dy0 = bl_word(m, &a);
            uint16_t w = (uint16_t)((bl_word(m, &a) & 0x1FFF) + 1), h = (uint16_t)((bl_word(m, &a) & 0x0FFF) + 1);
            a += 4;
            uint16_t dx1 = (uint16_t)(dx0 + w - 1), dy1 = (uint16_t)(dy0 + h - 1);
            if (dx0 > max_x || dx1 < min_x || dy0 > max_y || dy1 < min_y) { bl_idle(b, &ns, BL_DRAW_OP_BYTES); break; }
            uint16_t cminx = (uint16_t)min_x, cminy = (uint16_t)min_y, cmaxx = (uint16_t)max_x, cmaxy = (uint16_t)max_y;
            if (dx0 < cminx) dx0 = cminx;
            if (dy0 < cminy) dy0 = cminy;
            if (dx1 > cmaxx) dx1 = cmaxx;
            if (dy1 > cmaxy) dy1 = cmaxy;
            w = (uint16_t)(dx1 - dx0 + 1); h = (uint16_t)(dy1 - dy0 + 1);
            b->idle_op_bytes = 0;
            int src_rows = bl_vram_rows(sx, sy, w, h), dst_rows = bl_vram_rows(dx0, dy0, w, h);
            dx0 = (uint16_t)(dx0 - (dx0 & 3));
            dx1 = (uint16_t)(dx1 + ((4 - ((dx1 + 1) & 3)) & 3));
            uint16_t dw = (uint16_t)(dx1 - dx0 + 1), dh = (uint16_t)(dy1 - dy0 + 1);
            uint32_t clk = (uint32_t)(w * h / 4 + dw * dh / 2 + src_rows * 6 + dst_rows * (20 + 11) + 12);
            ns += (uint64_t)clk * BL_VRAM_CLK_NS;
            break; }
        default: goto done;                                  /* MAME: popmessage + stop */
        }
    }
done:
    ns += ns / BL_HLINE_PERIOD_NS * BL_HLINE_DURATION_NS;    /* VRAM line fetches for video output */
    return ns;
}

/* ---------------- on-chip TMU (behavior per MAME sh4tmu.cpp, BSD-3) ----------------
 * TSTR 0xFFFFFE92; channel n: TCOR 0xFE94+12n, TCNT 0xFE98+12n, TCR 0xFE9C+12n (16-bit).
 * Counts down at Pphi/div, Pphi = CPU/4 (MD = 0); underflow reloads TCOR, sets UNF (TCR b8) and, with
 * UNIE (b5), requests TUNIn (INTEVT 0x400/0x420/0x440, priority IPRA nibbles 15:12/11:8/7:4). */
static inline uint32_t oc32(CV1K *m, uint32_t o) { uint8_t *q = &m->onchip[o]; return (uint32_t)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3]; }
static inline void oc_set32(CV1K *m, uint32_t o, uint32_t v) { uint8_t *q = &m->onchip[o]; q[0] = v >> 24; q[1] = v >> 16; q[2] = v >> 8; q[3] = v; }
static inline uint16_t oc16(CV1K *m, uint32_t o) { return (uint16_t)(m->onchip[o] << 8 | m->onchip[o + 1]); }
static inline void oc_set16(CV1K *m, uint32_t o, uint16_t v) { m->onchip[o] = v >> 8; m->onchip[o + 1] = v; }
#define TCOR(n) (0xFE94 + 12 * (n))
#define TCNT(n) (0xFE98 + 12 * (n))
#define TCR(n)  (0xFE9C + 12 * (n))
static uint64_t tmu_tick(CV1K *m, int n, uint16_t tcr) { static const int d[8] = {4, 16, 64, 256, 1024, 1, 1, 1}; (void)m; (void)n; return 4u * d[tcr & 7]; }
static uint32_t tmu_count(CV1K *m, int n, uint16_t tcr)       /* MAME compute_ticks_timer: remaining*hz/div - 1 */
{
    uint64_t now = m->cpu.cycles, rem = m->tmu[n].next > now ? m->tmu[n].next - now : 0;
    return (uint32_t)((double)rem / (double)tmu_tick(m, n, tcr) - 1.0);
}
static void tmu_recompute(CV1K *m, int n) { m->tmu[n].next = m->cpu.cycles + ((uint64_t)oc32(m, TCNT(n)) + 1) * tmu_tick(m, n, oc16(m, TCR(n))); }

static void dmac_complete(CV1K *m);
void cv1k_timers(CV1K *m)
{
    dmac_complete(m);
    for (int n = 0; n < 3; n++)
        while (m->tmu[n].running && m->cpu.cycles >= m->tmu[n].next) {
            uint32_t tcor = oc32(m, TCOR(n));
            oc_set32(m, TCNT(n), tcor);
            m->tmu[n].next += ((uint64_t)tcor + 1) * tmu_tick(m, n, oc16(m, TCR(n)));
            uint16_t tcr = oc16(m, TCR(n)) | 0x100;
            oc_set16(m, TCR(n), tcr);
            if (tcr & 0x20) m->tmu[n].req = 1;
        }
}

uint64_t cv1k_next_event(CV1K *m)
{
    uint64_t e = ~0ull;
    for (int n = 0; n < 3; n++) if (m->tmu[n].running && m->tmu[n].next < e) e = m->tmu[n].next;
    for (int ch = 0; ch < 4; ch++) if (m->dma_te_pending[ch] && m->dma_te_at[ch] < e) e = m->dma_te_at[ch];
    return e;
}

/* called after the CPU wrote on-chip register bytes [o, o+len) (old values captured by the caller) */
static void tmu_written(CV1K *m, uint32_t o, uint32_t len, uint8_t old_tstr, const uint16_t *old_tcr)
{
    if (o <= 0xFE92 && o + len > 0xFE92) {
        uint8_t now = m->onchip[0xFE92], chg = now ^ old_tstr;
        for (int n = 0; n < 3; n++) if (chg & (1 << n)) {
            if (!(now & (1 << n))) { oc_set32(m, TCNT(n), tmu_count(m, n, oc16(m, TCR(n)))); m->tmu[n].running = 0; }
            else { m->tmu[n].running = 1; tmu_recompute(m, n); }
        }
    }
    for (int n = 0; n < 3; n++) {
        if (o <= TCNT(n) + 3 && o + len > TCNT(n) && m->tmu[n].running) tmu_recompute(m, n);
        if (o <= TCR(n) + 1 && o + len > TCR(n)) {
            uint16_t tcr = oc16(m, TCR(n));
            if (m->tmu[n].running) {
                /* MAME: tcnt = remaining with the OLD divider, then recompute with the new one */
                oc_set32(m, TCNT(n), tmu_count(m, n, old_tcr[n]));
                tmu_recompute(m, n);
            }
            if (!(tcr & 0x20) || !(tcr & 0x100)) m->tmu[n].req = 0;
        }
    }
}

/* ---------------- memory map ---------------- */
static inline uint8_t *plain(CV1K *m, uint32_t a)
{
    uint32_t p = P(a);
    if (a >= 0xFFFF0000u) return &m->onchip[a & 0xFFFF];
    if (a >= 0xE0000000u) return NULL;
    if (p >= RAM_BASE && p < 0x10000000u) return &m->ram[(p - RAM_BASE) & (m->ram_size - 1)];
    if (p < 0x00400000u) return &m->rom[p % m->rom_size];
    if (p >= 0x04000000u && p < 0x04010000u) return &m->area1[p & 0xFFFF];
    return NULL;
}

static uint32_t port_in(CV1K *m, uint32_t p, int *hit)
{
    *hit = 1;
    switch (p) {
    case 0x04000124: return m->in.c;
    case 0x04000126: return m->in.d;
    case 0x04000128: return 0xDF | 0x20;    /* NAND ready (always, timing model later) */
    case 0x0400012A: return m->in.f;
    case 0x04000130: return 0xFF;           /* port J: FPGA */
    case 0x04000134: return m->in.l;
    }
    *hit = 0; return 0;
}

static uint32_t dev_read(CV1K *m, uint32_t a, int size)
{
    uint32_t p = P(a);
    if (p == 0x10000000u) return nand_read(&m->nand);
    if (p >= 0x10000000u && p < 0x10000008u) return 0xFF;
    if (p == 0x10C00001u) return 0xFE | rtc_read_bit(&m->rtc);
    if (p >= 0x10C00000u && p < 0x10C00008u) return 0;
    if (p >= 0x18000000u && p < 0x18000058u) {
        switch (p & 0xFC) {
        case 0x10: return m->cpu.cycles >= m->blit.busy_until ? 0x10 : 0;   /* ready unless busy (blit_duration_ns) */
        case 0x24: case 0x28: return 0xFFFFFFFFu;
        case 0x50: return m->in.dsw;
        }
        return 0;
    }
    (void)size;
    return 0;
}

static void dev_write(CV1K *m, uint32_t a, uint32_t v, int size)
{
    uint32_t p = P(a);
    if (p >= 0x10000000u && p < 0x10000008u) {
        switch (p & 7) { case 0: nand_write(&m->nand, v); break; case 1: nand_cmd(&m->nand, v); break; case 2: nand_addr(&m->nand, v); break; }
        return;
    }
    if (p >= 0x10400000u && p < 0x10400008u) { m->sound_writes++; return; }   /* YMZ770: milestone 9 */
    if (p == 0x10C00001u) { rtc_write(&m->rtc, v); return; }
    if (p >= 0x18000000u && p < 0x18000058u) {
        Blitter *b = &m->blit;
        switch (p & 0xFC) {
        case 0x04: if (v & 1) { b->execs++; b->last_list = b->gfx_addr;
            if (m->timing_on) { uint64_t ns = blit_duration_ns(m); b->last_delay_ns = ns; b->busy_until = m->cpu.cycles + (ns * 1024 + 9999) / 10000; }   /* 102.4 cycles/us */
            if (b->on_exec) b->on_exec(b->ctx, b->gfx_addr); } break;
        case 0x08: b->gfx_addr = v; break;
        case 0x14: b->scroll_x = v; break;
        case 0x18: b->scroll_y = v; break;
        case 0x40: b->clip_x = v; break;
        case 0x44: b->clip_y = v; break;
        }
        return;
    }
    (void)size;
}

/* ---------------- on-chip DMAC (4 channels at 0xA4000020 + n*0x10: SAR, DAR, DMATCR, CHCR; DMAOR 0xA4000060) */
static uint8_t bus_rb(void *c, uint32_t a); static uint16_t bus_rw(void *c, uint32_t a); static uint32_t bus_rl(void *c, uint32_t a);
static void bus_wb(void *c, uint32_t a, uint8_t v); static void bus_ww(void *c, uint32_t a, uint16_t v); static void bus_wl(void *c, uint32_t a, uint32_t v);
static uint32_t a1_32(CV1K *m, uint32_t o) { uint8_t *q = &m->area1[o]; return (uint32_t)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3]; }
static void a1_set32(CV1K *m, uint32_t o, uint32_t v) { uint8_t *q = &m->area1[o]; q[0] = v >> 24; q[1] = v >> 16; q[2] = v >> 8; q[3] = v; }

static void dmac_kick(CV1K *m, int ch)
{
    uint32_t base = 0x20 + ch * 0x10;
    uint32_t chcr = a1_32(m, base + 0xC);
    uint16_t dmaor = (uint16_t)(m->area1[0x60] << 8 | m->area1[0x61]);
    if (!(chcr & 1) || (chcr & 2) || !(dmaor & 1) || m->dma_te_pending[ch]) return;   /* DE, !TE, DME, not in flight */
    uint32_t sar = a1_32(m, base), dar = a1_32(m, base + 4), n = a1_32(m, base + 8) & 0xFFFFFF;
    if (!n) n = 0x1000000;
    static const int sizes[4] = {1, 2, 4, 16};
    int sz = sizes[(chcr >> 3) & 3];
    int sm = (chcr >> 12) & 3, dm = (chcr >> 14) & 3;
    int sinc = sm == 1 ? sz : sm == 2 ? -sz : 0, dinc = dm == 1 ? sz : dm == 2 ? -sz : 0;
    m->dma_active = 1;
    for (uint32_t i = 0; i < n; i++) {
        if (sz == 1) bus_wb(m, dar, bus_rb(m, sar));
        else if (sz == 2) bus_ww(m, dar, bus_rw(m, sar));
        else for (int k = 0; k < sz; k += 4) bus_wl(m, dar + (dinc ? k : 0), bus_rl(m, sar + (sinc ? k : 0)));
        sar += sinc; dar += dinc;
    }
    m->dma_bytes += (uint64_t)n * sz; m->dma_count++; m->dma_active = 0;
    a1_set32(m, base, sar); a1_set32(m, base + 4, dar); a1_set32(m, base + 8, 0);
    if (m->timing_on) {   /* MAME (sh4dmac.cpp, BSD-3): data moves now, TE (+DEI) after 2*count+1 CPU cycles */
        m->dma_te_pending[ch] = 1; m->dma_te_at[ch] = m->cpu.cycles + 2ull * n + 1;
        return;
    }
    a1_set32(m, base + 0xC, chcr | 2);                                /* TE */
    if (chcr & 4) m->dma_irq_pending |= 1 << ch;                       /* IE: DEI interrupt (not routed yet) */
}

static void dmac_complete(CV1K *m)
{
    for (int ch = 0; ch < 4; ch++)
        if (m->dma_te_pending[ch] && m->cpu.cycles >= m->dma_te_at[ch]) {
            uint32_t base = 0x20 + ch * 0x10, chcr = a1_32(m, base + 0xC);
            m->dma_te_pending[ch] = 0;
            a1_set32(m, base + 8, 0); a1_set32(m, base + 0xC, chcr | 2);    /* TCR = 0, TE */
            if (chcr & 4) m->dma_irq_pending |= 1 << ch;
        }
}

static void area1_written(CV1K *m, uint32_t p)
{
    uint32_t o = p & 0xFFFF;
    if (o >= 0x20 && o < 0x60) dmac_kick(m, (o - 0x20) >> 4);
    else if (o == 0x60 || o == 0x61) for (int ch = 0; ch < 4; ch++) dmac_kick(m, ch);
}

static void port_out(CV1K *m, uint32_t p, uint32_t v)
{
    if (p == 0x04000130u) {                 /* port J: FPGA configuration bitstream */
        Blitter *b = &m->blit;
        if ((v & 0x08) && !(b->fw_port & 0x10) && (v & 0x10)) b->fw_pos++;
        b->fw_port = v;
    }
}

static void log_reg(CV1K *m, uint32_t a)
{
    uint32_t p = a & 0x1FFFFFFFu;
    if (!m->log_regs || !(a >= 0xFFFF0000u || (p >= 0x04000000u && p < 0x04010000u))) return;
    for (int i = 0; i < m->reg_used; i++) if (m->reg_addr[i] == a) { m->reg_n[i]++; return; }
    if (m->reg_used < 64) { m->reg_addr[m->reg_used] = a; m->reg_n[m->reg_used++] = 1; }
}

#define TMU_RANGE(a) ((a) >= 0xFFFFFE90u && (a) <= 0xFFFFFEBBu)
#define TMU_PRE(m) uint8_t ot = (m)->onchip[0xFE92]; uint16_t otcr[3] = {oc16((m), TCR(0)), oc16((m), TCR(1)), oc16((m), TCR(2))}
static uint32_t tmu_read32(CV1K *m, uint32_t a, uint32_t v)
{
    for (int n = 0; n < 3; n++) if ((a & 0xFFFF) == TCNT(n) && m->tmu[n].running) return tmu_count(m, n, oc16(m, TCR(n)));
    return v;
}

#define TM(m, a, w) do { if ((m)->timing_on && !(m)->dma_active) (m)->cpu.cycles += tm_data(&(m)->tm, (a), (w)); } while (0)

static void onchip_written(CV1K *m, uint32_t a, uint32_t v)
{
    if (!m->timing_on) return;
    if (a >= 0xF0000000u && a < 0xF1000000u) { tm_cache_array_write(&m->tm, a - 0xF0000000u, v); m->cpu.cycles += m->tm.pending_cycles; m->tm.pending_cycles = 0; }
    else if (a == 0xFFFFFFECu) tm_ccr_write(&m->tm, v);
}

static uint16_t bus_fetch(void *c, uint32_t a)
{
    CV1K *m = c; uint8_t *q = plain(m, a); return q ? (uint16_t)(q[0] << 8 | q[1]) : 0;
}

static uint32_t fetch_hook(void *c, uint32_t pc, uint16_t op)
{
    CV1K *m = c;
    return tm_fetch_charged(op) ? tm_fetch(&m->tm, pc) : 0;
}

static uint8_t bus_rb(void *c, uint32_t a)
{
    CV1K *m = c; TM(m, a, 0); log_reg(m, a); int hit; uint32_t v = port_in(m, P(a), &hit); if (hit) return v;
    uint8_t *q = plain(m, a); return q ? *q : dev_read(m, a, 1);
}
static uint16_t bus_rw(void *c, uint32_t a)
{
    CV1K *m = c; TM(m, a, 0); log_reg(m, a); uint8_t *q = plain(m, a); return q ? (uint16_t)(q[0] << 8 | q[1]) : dev_read(m, a, 2);
}
static uint32_t bus_rl(void *c, uint32_t a)
{
    CV1K *m = c; TM(m, a, 0); log_reg(m, a); uint8_t *q = plain(m, a);
    uint32_t v = q ? (uint32_t)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3] : dev_read(m, a, 4);
    return TMU_RANGE(a) ? tmu_read32(m, a, v) : v;
}
static void bus_wb(void *c, uint32_t a, uint8_t v)
{
    CV1K *m = c; TM(m, a, 1); uint8_t *q = plain(m, a);
    if (TMU_RANGE(a)) { TMU_PRE(m); *q = v; tmu_written(m, a & 0xFFFF, 1, ot, otcr); return; }
    if (q && P(a) >= 0x00400000u) { *q = v; if (P(a) >= 0x04000120u && P(a) < 0x04000140u) port_out(m, P(a), v); else if ((P(a) & 0xFFFF0000u) == 0x04000000u) area1_written(m, P(a)); }
    else if (!q) dev_write(m, a, v, 1);
}
static void bus_ww(void *c, uint32_t a, uint16_t v)
{
    CV1K *m = c; TM(m, a, 1); uint8_t *q = plain(m, a);
    if (TMU_RANGE(a)) { TMU_PRE(m); q[0] = v >> 8; q[1] = v; tmu_written(m, a & 0xFFFF, 2, ot, otcr); return; }
    if (q && P(a) >= 0x00400000u) { q[0] = v >> 8; q[1] = v; if ((P(a) & 0xFFFF0000u) == 0x04000000u) area1_written(m, P(a)); } else if (!q) dev_write(m, a, v, 2);
}
static void bus_wl(void *c, uint32_t a, uint32_t v)
{
    CV1K *m = c; TM(m, a, 1); if (a >= 0xF0000000u) onchip_written(m, a, v); uint8_t *q = plain(m, a);
    if (TMU_RANGE(a)) { TMU_PRE(m); q[0] = v >> 24; q[1] = v >> 16; q[2] = v >> 8; q[3] = v; tmu_written(m, a & 0xFFFF, 4, ot, otcr); return; }
    if (q && P(a) >= 0x00400000u) { q[0] = v >> 24; q[1] = v >> 16; q[2] = v >> 8; q[3] = v; if ((P(a) & 0xFFFF0000u) == 0x04000000u) area1_written(m, P(a)); } else if (!q) dev_write(m, a, v, 4);
}

/* ---------------- interrupts (IRQ2 = vsync; INTEVT2 0x640; priority IPRC[11:8]) ---------------- */
void cv1k_vsync(CV1K *m)
{
    m->irq2_pending = 1;
    m->area1[0x0004] |= 0x04;               /* IRR0.IRQ2R */
    m->frame++;
}

int cv1k_check_irq(CV1K *m)
{
    SH3 *s = &m->cpu;
    if (s->sr & SR_BL) return 0;
    uint32_t imask = (s->sr >> 4) & 15, best = 0, code = 0;
    int src = -1;
    uint32_t p2 = m->area1[0x0016] & 0x0F;                       /* IPRC[11:8]: IRQ2 */
    if (m->irq2_pending && p2 > imask) { best = p2; src = 3; code = 0x640; }
    uint16_t ipra = oc16(m, 0xFEE2);
    for (int n = 0; n < 3; n++) {
        uint32_t p = (ipra >> (12 - 4 * n)) & 15;
        if (m->tmu[n].req && p > imask && p > best) { best = p; src = n; code = 0x400 + 0x20 * n; }
    }
    if (src < 0) return 0;
    m->irq_by_src[src]++;
    uint32_t intevt;
    if (src == 3) { m->irq2_pending = 0; intevt = 0x3E0 - best * 0x20; }   /* IRQ2 HOLD_LINE: cleared on acknowledge */
    else intevt = code;                                                     /* TUNI stays requested until UNF is cleared */
    m->area1[0] = 0; m->area1[1] = 0; m->area1[2] = code >> 8; m->area1[3] = code;      /* INTEVT2 */
    m->onchip[0xFFD8] = intevt >> 24; m->onchip[0xFFD9] = intevt >> 16; m->onchip[0xFFDA] = intevt >> 8; m->onchip[0xFFDB] = intevt;
    if (m->irq_log && m->irq_log_n < m->irq_log_cap) m->irq_log[m->irq_log_n++] = s->cycles;
    sh3_interrupt(s, 0x600, 0);
    m->irqs_taken++;
    return 1;
}

void cv1k_take_trap(CV1K *m)
{
    SH3 *s = &m->cpu;
    uint32_t tra = (uint32_t)(s->trap - 1) << 2, ev = 0x160;
    s->trap = 0;
    m->onchip[0xFFD0] = 0; m->onchip[0xFFD1] = 0; m->onchip[0xFFD2] = tra >> 8; m->onchip[0xFFD3] = tra;
    m->onchip[0xFFD4] = 0; m->onchip[0xFFD5] = 0; m->onchip[0xFFD6] = ev >> 8; m->onchip[0xFFD7] = ev;
    sh3_interrupt(s, 0x100, 0);   /* SPC = s->pc (already the next instruction) */
}

/* ---------------- setup ---------------- */
static uint8_t *load(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*size);
    if (fread(p, 1, *size, f) != *size) { fclose(f); free(p); return NULL; }
    fclose(f); return p;
}

int cv1k_init(CV1K *m, const char *u4, const char *u2, uint32_t ram_size)
{
    memset(m, 0, sizeof *m);
    if (!(m->rom = load(u4, &m->rom_size))) return 0;
    for (uint32_t i = 0; i + 1 < m->rom_size; i += 2) { uint8_t t = m->rom[i]; m->rom[i] = m->rom[i + 1]; m->rom[i + 1] = t; }
    if (u2 && !(m->nand.data = load(u2, &m->nand.size))) return 0;
    m->ram_size = ram_size; m->ram = calloc(1, ram_size);
    for (int i = 0; i < 256; i++) m->rtc.eeprom[i] = 0xFFFF;
    m->in = (Inputs){0xFF, 0xFF, 0xFF, 0xFF, 0};
    m->cycles_per_frame = (uint64_t)(CV1K_CPU_HZ / CV1K_FRAME_HZ);
    m->cpu.bus = (SH3Bus){m, bus_rb, bus_rw, bus_rl, bus_wb, bus_ww, bus_wl, bus_fetch};
    tm_init(&m->tm, m->onchip);
    cv1k_set_timing(m, 1);
    cv1k_reset(m);
    return 1;
}

void cv1k_set_timing(CV1K *m, int on)
{
    m->timing_on = on;
    m->cpu.fetch_hook = on ? fetch_hook : 0;
    m->cpu.acct = on;
}

void cv1k_reset(CV1K *m)
{
    sh3_reset(&m->cpu);
    m->nand.status = 0xC0;
    m->next_vsync = m->cycles_per_frame;
}
