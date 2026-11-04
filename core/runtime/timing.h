#ifndef TIMING_H
#define TIMING_H
#include <stdint.h>
typedef struct { uint32_t tag; uint8_t lru, dirty; } TmLine;
typedef struct Timing {
    TmLine cache[256][4];
    uint32_t wb_address, last_area, last_page, wb_active, precharge, last_arg0;
    uint64_t pending_cycles;       /* charged by register-side effects (cache flush), added by caller */
    const uint8_t *onchip;         /* CPU on-chip registers (0xFFFF0000 + offset): BCR2/WCR1/WCR2/MCR */
    int enabled;
} Timing;
void tm_init(Timing *t, const uint8_t *onchip);
uint32_t tm_data(Timing *t, uint32_t addr, int write);   /* data access penalty */
uint32_t tm_fetch(Timing *t, uint32_t pc);                /* instruction fetch penalty */
void tm_cache_array_write(Timing *t, uint32_t offset, uint32_t data);
void tm_ccr_write(Timing *t, uint32_t data);
uint32_t tm_base_cycles(uint16_t op);
int tm_fetch_charged(uint16_t op);
#endif
