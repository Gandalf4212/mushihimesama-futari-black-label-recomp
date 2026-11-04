/* Headless runner: boots a CV1000 game and runs N frames on recompiled code (or the interpreter).
 * Usage: run_headless <u4> <u2> <ram_size hex> <main_entry hex> <frames> [--interp] [--report K]
 * GPL-2.0-or-later. */
#include "cv1k.h"
#include "dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t ram32(CV1K *m, uint32_t a) { uint8_t *q = m->ram + (a - 0x0C000000u); return (uint32_t)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3]; }

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: %s u4 u2 ram_size main_entry frames [--interp] [--report K]\n", argv[0]); return 2; }
    static CV1K m;
    if (!cv1k_init(&m, argv[1], argv[2], strtoul(argv[3], 0, 16))) return 1;
    uint32_t entry = strtoul(argv[4], 0, 16);
    uint64_t frames = strtoull(argv[5], 0, 10);
    int interp = 0, report = 60; const char *misses = NULL;
    for (int i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "--interp")) interp = 1;
        if (!strcmp(argv[i], "--report") && i + 1 < argc) report = atoi(argv[++i]);
        if (!strcmp(argv[i], "--misses") && i + 1 < argc) misses = argv[++i];
    }
    SH3 *s = &m.cpu;
    while ((s->pc & 0x1FFFFFFFu) != (entry & 0x1FFFFFFFu)) sh3_step(s);     /* boot */
    m.next_vsync = s->cycles + m.cycles_per_frame;
    if (!interp) dispatch_init(m.ram_size);
    clock_t t0 = clock();
    uint64_t last_blit = 0, last_nand = 0;
    while (m.frame < frames) {
        s->event_at = m.next_vsync;
        if (s->sleeping) { s->cycles = m.next_vsync; }
        else if (interp) sh3_step(s);
        else dispatch_step(s);
        if (s->trap) cv1k_take_trap(&m);
        if (s->cycles >= m.next_vsync) {
            cv1k_vsync(&m); m.next_vsync += m.cycles_per_frame;
            if (report && m.frame % report == 0)
                printf("frame %5llu  pc=%08x  idlevar=%08x  irqs=%llu  blits=+%llu  nand=+%lluKB  snd=%llu  dma=%llu/%lluKB  fpga_bits=%u\n",
                       (unsigned long long)m.frame, s->pc, ram32(&m, 0x0C002310u), (unsigned long long)m.irqs_taken,
                       (unsigned long long)(m.blit.execs - last_blit), (unsigned long long)((m.nand.bytes_read - last_nand) >> 10),
                       (unsigned long long)m.sound_writes, (unsigned long long)m.dma_count, (unsigned long long)(m.dma_bytes >> 10), m.blit.fw_pos),
                last_blit = m.blit.execs, last_nand = m.nand.bytes_read;
        }
        cv1k_check_irq(&m);
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("%llu frames in %.2f s (%.0f fps), %llu instructions\n", (unsigned long long)frames, secs, frames / secs,
           (unsigned long long)s->insns);
    if (!interp) { dispatch_report(); if (misses) dispatch_write_misses(misses); }
    return 0;
}
