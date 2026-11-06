/* CV1000 machine: memory map, on-chip INTC subset, ports, NAND, RTC9701, blitter registers.
 * Hardware behavior documented by the MAME project (BSD-3-Clause: cv1k.cpp, cv1k_v.cpp, sh3comn.cpp,
 * sh4comn.cpp, nandflash.cpp, rtc9701.cpp); this is an independent implementation. GPL-2.0-or-later. */
#ifndef CV1K_H
#define CV1K_H
#include "../sh3/sh3.h"
#include "timing.h"

/* MAME 0.289 cv1k: set_refresh(60.024 Hz), 512 total lines, visarea 0-239, vblank_time 0: VBLANK (IRQ2)
   ticks on frame boundaries (k / 60.024 s, measured). The real board is 60.0183806 Hz (6.4 MHz / (407*262)). */
#define CV1K_FRAME_HZ 60.024
#define CV1K_HW_FRAME_HZ 60.0183806291
#define CV1K_VBL_FRAC 1.0   /* VBLANK k (0-based) at (k + 1) / 60.024 s: measured in MAME (flaglog: flip at frame 501 = 501.000 / 60.024 s) */
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
    uint64_t busy_until; uint32_t idle_op_bytes; uint64_t last_delay_ns;   /* busy-time model (MAME cv1k_v.cpp) */
    uint32_t fw_port, fw_pos; uint8_t fw_sum_valid;
    void (*on_exec)(void *ctx, uint32_t list_addr);  /* renderer hook */
    void *ctx;
} Blitter;

typedef struct {
    uint8_t c, d, f, l;                     /* active-low input ports as read by the game */
    uint32_t dsw;
} Inputs;

#define CV1K_SND_BUF 16384                  /* stereo frames in the output ring (~1 s) */
#define CV1K_SND_CLOCK 16384000u            /* YMZ770 clock; output rate = clock / 1024 = 16 kHz */
struct Ymz770;
typedef struct CV1K {
    SH3 cpu;
    uint8_t *rom; uint32_t rom_size;        /* byte-swapped to big-endian at load */
    uint8_t *ram; uint32_t ram_size;
    uint8_t onchip[0x10000];                /* 0xFFFF0000-0xFFFFFFFF */
    uint8_t area1[0x10000];                 /* 0x04000000-0x0400FFFF (INTC2, ports) */
    Nand nand; Rtc9701 rtc; Blitter blit; Inputs in;
    Timing tm; int timing_on, dma_active;
    int log_regs; uint32_t reg_addr[64]; uint64_t reg_n[64]; int reg_used;   /* debug: on-chip reads */
    struct { uint64_t next; int running, req; } tmu[3];   /* on-chip TMU (TCOR/TCNT/TCR live in onchip[]) */
    uint64_t irq_by_src[4];
    int irq2_pending;
    uint32_t dma_irq_pending; uint64_t dma_bytes, dma_count;
    uint64_t dma_te_at[4]; uint8_t dma_te_pending[4];   /* MAME sh4dmac.cpp: TE set 2*count+1 cycles after start */
    uint64_t frame, irqs_taken, sound_writes;
    struct Ymz770 *ymz; uint8_t *snd_rom; uint32_t snd_rom_size;   /* YMZ770 (core/sound) */
    uint64_t snd_next; uint64_t snd_samples;                     /* CPU cycle of the next output sample */
    int16_t snd_buf[CV1K_SND_BUF * 2]; uint32_t snd_head, snd_tail;
    uint64_t cycles_per_frame;
    uint64_t next_vsync;
    uint32_t idle_pc, idle_ram;             /* MAME install_speedups: a read of idle_ram..+7 at pc() == idle_pc(+2) spins 10 us */
    uint64_t spin_to;                       /* pending spin end (cycles), 0 = none */
    uint64_t *irq_log; uint32_t irq_log_n, irq_log_cap;   /* cycle of each IRQ taken (lockstep replay) */
} CV1K;

int  cv1k_init(CV1K *m, const char *u4, const char *u2, uint32_t ram_size);
void cv1k_reset(CV1K *m);
void cv1k_set_timing(CV1K *m, int on);   /* MAME 0.289 cache/bus model on/off */
/* Take a pending interrupt if allowed. Returns 1 if taken. Call only at dispatch boundaries. */
int  cv1k_check_irq(CV1K *m);
uint64_t cv1k_vsync_at(uint64_t k);           /* cycle of the k-th (0-based) VBLANK, MAME schedule */
void cv1k_set_idle(CV1K *m, uint32_t idle_pc, uint32_t idle_ram);
/* Signal vsync (IRQ2 assert) and advance the frame counter. */
void cv1k_vsync(CV1K *m);
/* TRAPA exception (s->trap set by the instruction): TRA, EXPEVT 0x160, VBR+0x100. */
void cv1k_take_trap(CV1K *m);
/* TMU: process underflows up to the current cycle; next_event returns the next timer cycle (or ~0). */
void cv1k_timers(CV1K *m);
uint64_t cv1k_next_event(CV1K *m);
/* Sound: load the YMZ770 ROM from the u23/u24 dumps (word-swapped like MAME's ROM_LOAD16_WORD_SWAP),
 * or from an already swapped image (assets/sound.bin). Without it the chip is silent. */
int  cv1k_load_sound(CV1K *m, const char *u23, const char *u24);
int  cv1k_load_sound_image(CV1K *m, const char *bin);
int  cv1k_save_sound_image(CV1K *m, const char *bin);
/* Render YMZ770 samples up to the current CPU cycle into the output ring (also done before every
 * chip write, so register changes land on the exact sample). Call at least once per frame. */
void cv1k_sound_catchup(CV1K *m);
/* Take up to max stereo frames (interleaved L,R) from the ring; returns the number taken. */
int  cv1k_sound_read(CV1K *m, int16_t *out, int max);
#endif
