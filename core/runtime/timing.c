/* SH7709S cache + bus timing model. Behavior matches MAME 0.289 sh7709s.cpp (BSD-3-Clause, Jesus Ramos),
 * including its quirks, so frame timing can be compared with MAME. Independent implementation.
 * See docs/timing.md. GPL-2.0-or-later. */
#include "timing.h"
#include <string.h>

#define LINE 16u
#define WAYS 4
#define SETS 256              /* 16 KB / 16 / 4 */
#define SDRAM_PAGE 1024u
#define CACHE_MISS_STALL 1u
#define BUS 3u
#define BURST_RD 3u
#define BURST_WR 4u
#define AM 0x1FFFFFFFu

static inline uint16_t reg16(const Timing *t, uint32_t off) { return (uint16_t)(t->onchip[off] << 8 | t->onchip[off + 1]); }
#define BCR2 reg16(t, 0xFF62)
#define WCR1 reg16(t, 0xFF64)
#define WCR2 reg16(t, 0xFF66)
#define MCR  reg16(t, 0xFF68)

static inline int cacheable(uint32_t a) { uint32_t r = a >> 29; return r != 5 && r != 7; }
static inline uint32_t area_of(uint32_t a) { return (a & AM) >> 26; }
static inline int sdram(uint32_t a) { return area_of(a) == 3; }

static uint32_t wcr1_t(const Timing *t, uint32_t a)
{
    uint32_t ar = area_of(a);
    if (ar > 6 || ar == 1) return 0;
    uint32_t v = (WCR1 >> (ar * 2)) & 3;
    return v ? v : 1;
}

static uint32_t wcr2_t(const Timing *t, uint32_t a)
{
    uint32_t ar = area_of(a), w = WCR2, v;
    if (ar > 6 || ar == 1) return 0;
    if (ar == 0) v = w & 7;
    else if (ar == 2 || ar == 3) { w >>= 3; if (ar == 3) w >>= 2; v = w & 3; return v ? v : 1; }
    else v = (w >> (7 + (ar - 4) * 3)) & 7;
    static const uint8_t burst[8] = {2, 2, 3, 4, 4, 6, 8, 10}, plain[8] = {0, 1, 2, 3, 4, 6, 8, 10};
    return sdram(a) ? burst[v] : plain[v];
}

static inline uint32_t tpc(const Timing *t)  { return ((MCR >> 14) & 3) + 1; }
static inline uint32_t rcd(const Timing *t)  { return ((MCR >> 12) & 3) + 1; }
static inline uint32_t trwl(const Timing *t) { return ((MCR >> 10) & 3) + 1; }

static uint32_t line_fetches(const Timing *t, uint32_t a)
{
    uint32_t ar = area_of(a);
    if (ar == 0) return 8;
    if (ar == 1) return 0;
    return LINE >> ((BCR2 >> (ar * 2)) & 3);
}

static int cache_access(Timing *t, uint32_t a, int write)
{
    if (!cacheable(a)) return 0;
    uint32_t line = (a & AM) / LINE, set = line % SETS;
    TmLine *s = t->cache[set];
    for (int i = 0; i < WAYS; i++)
        if (s[i].tag == line) {
            if (write) s[i].dirty = 1;
            if (s[i].lru != WAYS - 1) {
                for (int j = 0; j < WAYS; j++) if (s[j].lru > s[i].lru) s[j].lru--;
                s[i].lru = WAYS - 1;
            }
            return 1;
        }
    for (int i = 0; i < WAYS; i++)
        if (s[i].lru == 0) {
            if (s[i].dirty) t->wb_address = s[i].tag * LINE;
            s[i].tag = line; s[i].lru = WAYS - 1; s[i].dirty = write ? 1 : 0;
            for (int j = 0; j < WAYS; j++) if (i != j && s[j].lru != 0) s[j].lru--;
            break;
        }
    return 0;
}

static uint32_t penalty(Timing *t, uint32_t a, int write)
{
    uint32_t ar = area_of(a);
    if (cache_access(t, a, write)) return 0;
    uint32_t bus = BUS + BURST_RD + rcd(t) + wcr2_t(t, a);
    uint32_t cpu = cacheable(a) ? CACHE_MISS_STALL : 0;
    uint32_t page = a / SDRAM_PAGE;
    if (!sdram(a)) {
        if (!cacheable(a)) bus = BUS + wcr2_t(t, a);
        else bus = (BUS + wcr2_t(t, a)) * line_fetches(t, a);
    } else if (!cacheable(a)) {
        bus = write ? BUS + rcd(t) + trwl(t) : BUS + rcd(t) + wcr2_t(t, a);
    }
    if (ar != t->last_area) bus += wcr1_t(t, a);
    t->last_area = ar;
    if (sdram(a) && page == t->last_page && t->precharge > 0) cpu += t->precharge;
    t->precharge = 0;
    if (t->wb_active > 0) { cpu += t->wb_active; t->wb_active = 0; }
    if (sdram(a)) t->last_page = page;
    if (t->wb_address != 0) {
        if (sdram(a)) {
            uint32_t wpage = t->wb_address / SDRAM_PAGE;
            if (page == wpage) t->wb_active += tpc(t) * 2;
            t->wb_active += (BUS + wcr1_t(t, a) + BURST_WR + rcd(t) + trwl(t)) * 2;
            t->last_page = wpage;
        } else {
            t->wb_active += wcr1_t(t, a) * 2;
            t->wb_active += ((BUS + wcr2_t(t, a)) * line_fetches(t, a)) * 2;
        }
        t->wb_address = 0;
    } else if (sdram(a)) {
        t->precharge = tpc(t) * 2;
    }
    return cpu + bus * 2;
}

void tm_init(Timing *t, const uint8_t *onchip)
{
    memset(t, 0, sizeof *t);
    t->onchip = onchip;
    t->enabled = 1;
}

uint32_t tm_data(Timing *t, uint32_t a, int write)
{
    /* MAME quirk: accesses at >= 0xE0000000 don't update arg0, so the previous address is charged */
    if (a < 0xE0000000u) t->last_arg0 = a;
    return penalty(t, t->last_arg0, write);
}

uint32_t tm_fetch(Timing *t, uint32_t pc)
{
    if (t->wb_active > 0) { if (--t->wb_active == 0) t->precharge = tpc(t) * 2; }
    else if (t->precharge > 0) t->precharge--;
    return penalty(t, pc, 0);
}

void tm_cache_array_write(Timing *t, uint32_t offset, uint32_t data)
{
    if (data & 2) return;                               /* not a flush */
    uint32_t idx = (offset & 0x00FFFFFFu) / 16;          /* MAME: dword offset / 4 */
    uint32_t set = idx % SETS, way = (idx / SETS) % WAYS;
    TmLine *e = &t->cache[set][way];
    if (e->dirty) {
        uint32_t wb = e->tag * LINE;
        e->dirty = 0;
        t->pending_cycles += (BUS + BURST_WR + wcr1_t(t, wb) + rcd(t) + trwl(t)) * 2;
        t->last_area = area_of(wb);
    }
}

void tm_ccr_write(Timing *t, uint32_t data)
{
    if (data & 8) memset(t->cache, 0, sizeof t->cache);
}

/* Base instruction cycles as in MAME's SH front end (sh_fe.cpp). */
uint32_t tm_base_cycles(uint16_t op)
{
    switch (op >> 12) {
    case 0x0:
        switch (op & 0xFF) { case 0x0B: return 2; /* rts */ case 0x1B: return 3; /* sleep */ case 0x2B: return 4; /* rte */ }
        if ((op & 0xFF) == 0x23) return 2;                        /* braf */
        if ((op & 0xF) == 0x7) return 2;                          /* mul.l */
        if ((op & 0xF) == 0xF) return 3;                          /* mac.l */
        return 1;
    case 0x2: return ((op & 0xF) == 0xE || (op & 0xF) == 0xF) ? 2 : 1;   /* mulu.w, muls.w */
    case 0x3: return ((op & 0xF) == 0x5 || (op & 0xF) == 0xD) ? 2 : 1;   /* dmulu.l, dmuls.l */
    case 0x4:
        if ((op & 0xF) == 0xF) return 3;                          /* mac.w */
        switch (op & 0xFF) { case 0x03: return 2; /* stc.l sr */ case 0x07: return 3; /* ldc.l sr */ case 0x1B: return 4; /* tas.b */ }
        return 1;
    case 0x8: { uint32_t s = (op >> 8) & 0xF; if (s == 0x9 || s == 0xB) return 3; if (s == 0xD || s == 0xF) return 2; return 1; }
    case 0xA: case 0xB: return 2;                                 /* bra, bsr */
    case 0xC: return ((op >> 8) & 0xF) == 0x3 ? 8 : 1;           /* trapa */
    default: return 1;
    }
}

int tm_fetch_charged(uint16_t op)
{
    uint32_t g = op >> 12;
    return g == 0x0 || g == 0x4 || g == 0xF;          /* MAME hooks the fetch only in groups 0, 4 and 15 */
}
