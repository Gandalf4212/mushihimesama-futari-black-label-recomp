/* CV1000 machine: memory map, on-chip INTC subset, ports, NAND, RTC9701, blitter registers.
 * Hardware behavior documented by the MAME project (BSD-3-Clause: cv1k.cpp, cv1k_v.cpp, sh3comn.cpp,
 * sh4comn.cpp, nandflash.cpp, rtc9701.cpp); this is an independent implementation. GPL-2.0-or-later. */
#ifndef CV1K_H
#define CV1K_H
#include "../sh3/sh3.h"
#include "timing.h"

#define CV1K_FRAME_HZ 60.0183806291
#define CV1K_CPU_HZ   102400000.0

typedef struct {
    uint8_t *data; uint32_t size;          /* raw dump incl. spare: pages * 2112 */
    int mode, addr_ptr; uint32_t col, row; uint8_t status;
    uint8_t pagereg[2112];
    int dirty;                              /* settings written: caller may persist */
    uint64_t bytes_read;
} Nand;

typedef struct {
    int cs, clk, latch, state, pos; uint32_t cmd, addr, data; int dpos;
    uint16_t eeprom[256]; int dirty;
    uint8_t rtc[8];
} Rtc9701;

typedef struct {
    uint32_t gfx_addr, scroll_x, scroll_y, clip_x, clip_y;
    uint64_t execs; uint32_t last_list;
    uint32_t fw_port, fw_pos; uint8_t fw_sum_valid;
    void (*on_exec)(void *ctx, uint32_t list_addr);  /* renderer hook */
    void *ctx;
} Blitter;

typedef struct {
    uint8_t c, d, f, l;                     /* active-low input ports as read by the game */
    uint32_t dsw;
} Inputs;

typedef struct CV1K {
    SH3 cpu;
    uint8_t *rom; uint32_t rom_size;        /* byte-swapped to big-endian at load */
    uint8_t *ram; uint32_t ram_size;
    uint8_t onchip[0x10000];                /* 0xFFFF0000-0xFFFFFFFF */
    uint8_t area1[0x10000];                 /* 0x04000000-0x0400FFFF (INTC2, ports) */
    Nand nand; Rtc9701 rtc; Blitter blit; Inputs in;
    Timing tm; int timing_on, dma_active;
    int irq2_pending;
    uint32_t dma_irq_pending; uint64_t dma_bytes, dma_count;
    uint64_t frame, irqs_taken, sound_writes;
    uint64_t cycles_per_frame;
    uint64_t next_vsync;
    uint64_t *irq_log; uint32_t irq_log_n, irq_log_cap;   /* cycle of each IRQ taken (lockstep replay) */
} CV1K;

int  cv1k_init(CV1K *m, const char *u4, const char *u2, uint32_t ram_size);
void cv1k_reset(CV1K *m);
void cv1k_set_timing(CV1K *m, int on);   /* MAME 0.289 cache/bus model on/off */
/* Take a pending interrupt if allowed. Returns 1 if taken. Call only at dispatch boundaries. */
int  cv1k_check_irq(CV1K *m);
/* Signal vsync (IRQ2 assert) and advance the frame counter. */
void cv1k_vsync(CV1K *m);
/* TRAPA exception (s->trap set by the instruction): TRA, EXPEVT 0x160, VBR+0x100. */
void cv1k_take_trap(CV1K *m);
#endif
