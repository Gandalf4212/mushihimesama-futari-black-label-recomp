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
        case 0x10: return 0x10;            /* blitter ready */
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
        case 0x04: if (v & 1) { b->execs++; b->last_list = b->gfx_addr; if (b->on_exec) b->on_exec(b->ctx, b->gfx_addr); } break;
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
    if (!(chcr & 1) || (chcr & 2) || !(dmaor & 1)) return;      /* DE, !TE, DME */
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
    a1_set32(m, base + 0xC, chcr | 2);                                /* TE */
    if (chcr & 4) m->dma_irq_pending |= 1 << ch;                       /* IE: DEI interrupt (not routed yet) */
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
    CV1K *m = c; TM(m, a, 0); int hit; uint32_t v = port_in(m, P(a), &hit); if (hit) return v;
    uint8_t *q = plain(m, a); return q ? *q : dev_read(m, a, 1);
}
static uint16_t bus_rw(void *c, uint32_t a)
{
    CV1K *m = c; TM(m, a, 0); uint8_t *q = plain(m, a); return q ? (uint16_t)(q[0] << 8 | q[1]) : dev_read(m, a, 2);
}
static uint32_t bus_rl(void *c, uint32_t a)
{
    CV1K *m = c; TM(m, a, 0); uint8_t *q = plain(m, a);
    return q ? (uint32_t)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3] : dev_read(m, a, 4);
}
static void bus_wb(void *c, uint32_t a, uint8_t v)
{
    CV1K *m = c; TM(m, a, 1); uint8_t *q = plain(m, a);
    if (q && P(a) >= 0x00400000u) { *q = v; if (P(a) >= 0x04000120u && P(a) < 0x04000140u) port_out(m, P(a), v); else if ((P(a) & 0xFFFF0000u) == 0x04000000u) area1_written(m, P(a)); }
    else if (!q) dev_write(m, a, v, 1);
}
static void bus_ww(void *c, uint32_t a, uint16_t v)
{
    CV1K *m = c; TM(m, a, 1); uint8_t *q = plain(m, a);
    if (q && P(a) >= 0x00400000u) { q[0] = v >> 8; q[1] = v; if ((P(a) & 0xFFFF0000u) == 0x04000000u) area1_written(m, P(a)); } else if (!q) dev_write(m, a, v, 2);
}
static void bus_wl(void *c, uint32_t a, uint32_t v)
{
    CV1K *m = c; TM(m, a, 1); if (a >= 0xF0000000u) onchip_written(m, a, v); uint8_t *q = plain(m, a);
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
    if (!m->irq2_pending || (s->sr & SR_BL)) return 0;
    uint32_t prio = (m->area1[0x0016] >> 0) & 0x0F;   /* IPRC high byte bits 11:8 = byte 0x16 low nibble */
    if (prio <= ((s->sr >> 4) & 15)) return 0;
    m->irq2_pending = 0;                                 /* HOLD_LINE: cleared on acknowledge */
    uint32_t intevt = 0x3E0 - prio * 0x20;
    m->area1[0] = 0; m->area1[1] = 0; m->area1[2] = 0x06; m->area1[3] = 0x40;          /* INTEVT2 = 0x640 */
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
