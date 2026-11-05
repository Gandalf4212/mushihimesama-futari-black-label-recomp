/* Headless runner: boots a CV1000 game and runs N frames on recompiled code (or the interpreter).
 * Usage: run_headless <u4> <u2> <ram_size hex> <main_entry hex> <frames> [--interp] [--report K]
 * GPL-2.0-or-later. */
#include "cv1k.h"
#include "dispatch.h"
#include "../render/blit_ref.h"
#include "../render/png.h"

static BlitRef g_blit;
static CV1K *g_m;
static void on_exec(void *ctx, uint32_t list) { (void)ctx; blit_ref_exec(&g_blit, g_m->ram, g_m->ram_size - 1, list, g_m->blit.clip_x, g_m->blit.clip_y); }
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Input script (shared with tools/mame/capture.lua): lines "start_frame duration TOKEN...".
 * Tokens: COIN1 COIN2 START1 START2 SERVICE TEST, UP DOWN LEFT RIGHT B1..B4 (P1), P2_UP..P2_B4. */
typedef struct { uint64_t start, dur; uint8_t c, d, l, f; } InEv;
static InEv g_ev[4096]; static int g_nev; static int64_t g_in_off;
static void load_inputs(const char *path)
{
    FILE *f = fopen(path, "r"); if (!f) { perror(path); exit(1); }
    char line[512];
    static const struct { const char *n; char port; uint8_t bit; } map[] = {
        {"SERVICE",'c',0x01},{"COIN1",'c',0x04},{"COIN2",'c',0x08},{"START1",'c',0x10},{"START2",'c',0x20},{"TEST",'f',0x02},
        {"UP",'d',1},{"DOWN",'d',2},{"LEFT",'d',4},{"RIGHT",'d',8},{"B1",'d',0x10},{"B2",'d',0x20},{"B3",'d',0x40},{"B4",'d',0x80},
        {"P2_UP",'l',1},{"P2_DOWN",'l',2},{"P2_LEFT",'l',4},{"P2_RIGHT",'l',8},{"P2_B1",'l',0x10},{"P2_B2",'l',0x20},{"P2_B3",'l',0x40},{"P2_B4",'l',0x80}};
    while (fgets(line, sizeof line, f) && g_nev < 4096) {
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *tok = strtok(line, " \t\r\n"); if (!tok) continue;
        InEv e = {strtoull(tok, 0, 10), 0, 0, 0, 0, 0};
        if (!(tok = strtok(NULL, " \t\r\n"))) continue;
        e.dur = strtoull(tok, 0, 10);
        while ((tok = strtok(NULL, " \t\r\n")))
            for (unsigned k = 0; k < sizeof map / sizeof map[0]; k++)
                if (!strcmp(tok, map[k].n)) { if (map[k].port == 'c') e.c |= map[k].bit; else if (map[k].port == 'd') e.d |= map[k].bit; else if (map[k].port == 'l') e.l |= map[k].bit; else e.f |= map[k].bit; }
        g_ev[g_nev++] = e;
    }
    fclose(f);
}
static void apply_inputs(CV1K *m)
{
    int64_t fr = (int64_t)m->frame + g_in_off;   /* script frames are MAME frame numbers */
    uint8_t c = 0, d = 0, l = 0, ff = 0;
    for (int i = 0; i < g_nev; i++)
        if (fr >= (int64_t)g_ev[i].start && fr < (int64_t)(g_ev[i].start + g_ev[i].dur)) { c |= g_ev[i].c; d |= g_ev[i].d; l |= g_ev[i].l; ff |= g_ev[i].f; }
    m->in.c = ~c; m->in.d = ~d; m->in.l = ~l; m->in.f = ~ff;   /* active low */
}

static uint64_t dispatch_insns_estimate(SH3 *s) { (void)s; return 0; }
static uint32_t ram32(CV1K *m, uint32_t a) { uint8_t *q = m->ram + (a - 0x0C000000u); return (uint32_t)q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3]; }

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage: %s u4 u2 ram_size main_entry frames [--interp] [--report K]\n", argv[0]); return 2; }
    static CV1K m;
    if (!cv1k_init(&m, argv[1], argv[2], strtoul(argv[3], 0, 16))) return 1;
    uint32_t entry = strtoul(argv[4], 0, 16);
    uint64_t frames = strtoull(argv[5], 0, 10);
    int interp = 0, timing = 1, report = 60; uint64_t st_from = ~0ull, st_to = 0, st_c0 = 0, st_i0 = 0; Timing st0; const char *misses = NULL, *pngdir = NULL; int png_every = 0; uint64_t png_from = 0, png_to = ~0ull;
    for (int i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "--interp")) interp = 1;
        if (!strcmp(argv[i], "--tmstats") && i + 2 < argc) { st_from = strtoull(argv[++i], 0, 10); st_to = strtoull(argv[++i], 0, 10); }
        if (!strcmp(argv[i], "--timing") && i + 1 < argc) timing = strcmp(argv[++i], "off") != 0;
        if (!strcmp(argv[i], "--report") && i + 1 < argc) report = atoi(argv[++i]);
        if (!strcmp(argv[i], "--misses") && i + 1 < argc) misses = argv[++i];
        if (!strcmp(argv[i], "--input") && i + 1 < argc) load_inputs(argv[++i]);
        if (!strcmp(argv[i], "--frame-offset") && i + 1 < argc) g_in_off = atoll(argv[++i]);
        if (!strcmp(argv[i], "--png-range") && i + 2 < argc) { png_from = strtoull(argv[++i], 0, 10); png_to = strtoull(argv[++i], 0, 10); }
        if (!strcmp(argv[i], "--png") && i + 2 < argc) { pngdir = argv[++i]; png_every = atoi(argv[++i]); }
    }
    SH3 *s = &m.cpu;
    g_m = &m; blit_ref_init(&g_blit); m.blit.on_exec = on_exec;
    static uint32_t fb[320 * 240];
    if (!timing) cv1k_set_timing(&m, 0);
    m.next_vsync = m.cycles_per_frame;                                       /* frames count from power-on, like MAME */
    while ((s->pc & 0x1FFFFFFFu) != (entry & 0x1FFFFFFFu)) {                 /* boot on the interpreter */
        sh3_step(s);
        if (s->cycles >= m.next_vsync) { cv1k_vsync(&m); m.next_vsync += m.cycles_per_frame; }
    }
    printf("boot reached main entry at frame %llu (cycle %llu)\n", (unsigned long long)m.frame, (unsigned long long)s->cycles);
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
            cv1k_vsync(&m); m.next_vsync += m.cycles_per_frame; if (g_nev) apply_inputs(&m);
            if (m.frame == st_from) { st0 = m.tm; st_c0 = s->cycles; st_i0 = s->insns + dispatch_insns_estimate(s); }
            if (m.frame == st_to) {
                Timing *t = &m.tm;
                printf("timing stats frames %llu-%llu: cycles %llu | base extra %llu | data pen %llu (%llu acc, %llu miss, %llu uncached) | fetch pen %llu (%llu charged, %llu miss, %llu uncached)\n",
                    (unsigned long long)st_from, (unsigned long long)st_to, (unsigned long long)(s->cycles - st_c0),
                    (unsigned long long)(t->st_base - st0.st_base), (unsigned long long)(t->st_data - st0.st_data), (unsigned long long)(t->st_n_data - st0.st_n_data),
                    (unsigned long long)(t->st_miss_data - st0.st_miss_data), (unsigned long long)(t->st_unc_data - st0.st_unc_data),
                    (unsigned long long)(t->st_fetch - st0.st_fetch), (unsigned long long)(t->st_n_fetch - st0.st_n_fetch),
                    (unsigned long long)(t->st_miss_fetch - st0.st_miss_fetch), (unsigned long long)(t->st_unc_fetch - st0.st_unc_fetch));
            }
            if (pngdir && png_every && m.frame % png_every == 0 && m.frame >= png_from && m.frame <= png_to) {
                char path[512]; snprintf(path, sizeof path, "%s/frame_%05llu.png", pngdir, (unsigned long long)m.frame);
                blit_ref_frame(&g_blit, m.blit.scroll_x, m.blit.scroll_y, fb); png_write_rgb(path, fb, 320, 240);
            }
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
    printf("saves: nand written=%d, eeprom written=%d\n", m.nand.dirty, m.rtc.dirty);
    printf("blitter: %llu draws, %llu uploads, %llu bad ops\n", (unsigned long long)g_blit.draws, (unsigned long long)g_blit.uploads, (unsigned long long)g_blit.bad_ops);
    printf("%llu frames in %.2f s (%.0f fps), %llu instructions\n", (unsigned long long)frames, secs, frames / secs,
           (unsigned long long)s->insns);
    if (!interp) { dispatch_report(); if (misses) dispatch_write_misses(misses); }
    return 0;
}
