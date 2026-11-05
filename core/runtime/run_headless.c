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
    int interp = 0, timing = 1, report = 60; uint64_t st_from = ~0ull, st_to = 0, st_c0 = 0, st_i0 = 0; Timing st0; const char *misses = NULL, *pngdir = NULL; int png_every = 0; const char *dump_dir = NULL; char dump_list[512] = ""; uint64_t png_from = 0, png_to = ~0ull;
    for (int i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "--interp")) interp = 1;
        if (!strcmp(argv[i], "--tmstats") && i + 2 < argc) { st_from = strtoull(argv[++i], 0, 10); st_to = strtoull(argv[++i], 0, 10); }
        if (!strcmp(argv[i], "--timing") && i + 1 < argc) timing = strcmp(argv[++i], "off") != 0;
        if (!strcmp(argv[i], "--report") && i + 1 < argc) report = atoi(argv[++i]);
        if (!strcmp(argv[i], "--misses") && i + 1 < argc) misses = argv[++i];
        if (!strcmp(argv[i], "--dump-ram") && i + 2 < argc) { dump_dir = argv[++i]; snprintf(dump_list, sizeof dump_list, ",%s,", argv[++i]); }
        if (!strcmp(argv[i], "--input") && i + 1 < argc) load_inputs(argv[++i]);
        if (!strcmp(argv[i], "--frame-offset") && i + 1 < argc) g_in_off = atoll(argv[++i]);
        if (!strcmp(argv[i], "--png-range") && i + 2 < argc) { png_from = strtoull(argv[++i], 0, 10); png_to = strtoull(argv[++i], 0, 10); }
        if (!strcmp(argv[i], "--png") && i + 2 < argc) { pngdir = argv[++i]; png_every = atoi(argv[++i]); }
    }
    SH3 *s = &m.cpu;
    g_m = &m; blit_ref_init(&g_blit); g_blit.mode_tag = 1; m.blit.on_exec = on_exec;
    static uint32_t fb[320 * 240];
    if (!timing) cv1k_set_timing(&m, 0);
    m.next_vsync = m.cycles_per_frame;                                       /* frames count from power-on, like MAME */
    while ((s->pc & 0x1FFFFFFFu) != (entry & 0x1FFFFFFFu)) {                 /* boot on the interpreter */
        sh3_step(s);
        cv1k_timers(&m);
        if (s->cycles >= m.next_vsync) { cv1k_vsync(&m); m.next_vsync += m.cycles_per_frame; }
    }
    printf("boot reached main entry at frame %llu (cycle %llu)\n", (unsigned long long)m.frame, (unsigned long long)s->cycles);
    if (!interp) dispatch_init(m.ram_size);
    clock_t t0 = clock();
    uint64_t last_blit = 0, last_nand = 0;
    while (m.frame < frames) {
        { uint64_t te = cv1k_next_event(&m); s->event_at = te < m.next_vsync ? te : m.next_vsync; }
        if (s->sleeping) { s->cycles = s->event_at; }
        else if (interp) sh3_step(s);
        else dispatch_step(s);
        if (s->trap) cv1k_take_trap(&m);
        cv1k_timers(&m);
        if (s->cycles >= m.next_vsync) {
            cv1k_vsync(&m); m.next_vsync += m.cycles_per_frame; if (g_nev) apply_inputs(&m);
            if (dump_dir) {
                char key[32]; snprintf(key, sizeof key, ",%llu,", (unsigned long long)m.frame);
                if (strstr(dump_list, key)) {
                    char path[512]; snprintf(path, sizeof path, "%s/ram_%05llu.bin", dump_dir, (unsigned long long)m.frame);
                    FILE *f = fopen(path, "wb"); if (f) { fwrite(m.ram, 1, m.ram_size, f); fclose(f); }
                }
            }
            if (m.frame == 2200) m.log_regs = 1;
            if (m.frame == 2530) g_blit.mode_tag = 2;   /* after mode select: character select onwards */
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
                printf("frame %5llu  sr=%08x tuni0=%llu tuni1=%llu pc=%08x  idlevar=%08x  irqs=%llu  blits=+%llu  nand=+%lluKB  snd=%llu  dma=%llu/%lluKB  fpga_bits=%u\n",
                       (unsigned long long)m.frame, s->sr, (unsigned long long)m.irq_by_src[0], (unsigned long long)m.irq_by_src[1], s->pc, ram32(&m, 0x0C002310u), (unsigned long long)m.irqs_taken,
                       (unsigned long long)(m.blit.execs - last_blit), (unsigned long long)((m.nand.bytes_read - last_nand) >> 10),
                       (unsigned long long)m.sound_writes, (unsigned long long)m.dma_count, (unsigned long long)(m.dma_bytes >> 10), m.blit.fw_pos),
                last_blit = m.blit.execs, last_nand = m.nand.bytes_read;
        }
        cv1k_check_irq(&m);
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    for (int i = 0; i < 128; i++) if (g_blit.mode_use[i]) printf("mode blend=%d s=%d d=%d used:%s%s\n", i >> 6, (i >> 3) & 7, i & 7, g_blit.mode_use[i] & 1 ? " before" : "", g_blit.mode_use[i] & 2 ? " charsel+" : "");
    for (int i = 0; i < m.reg_used; i++) printf("reg read %08x x%llu\n", m.reg_addr[i], (unsigned long long)m.reg_n[i]);
    printf("irqs by source: tuni0=%llu tuni1=%llu tuni2=%llu irq2=%llu; tmu running %d%d%d, TCR %04x %04x %04x TCOR %08x %08x %08x IPRA %04x\n", (unsigned long long)m.irq_by_src[0], (unsigned long long)m.irq_by_src[1], (unsigned long long)m.irq_by_src[2], (unsigned long long)m.irq_by_src[3], m.tmu[0].running, m.tmu[1].running, m.tmu[2].running, m.onchip[0xFE9C]<<8|m.onchip[0xFE9D], m.onchip[0xFEA8]<<8|m.onchip[0xFEA9], m.onchip[0xFEB4]<<8|m.onchip[0xFEB5], m.onchip[0xFE94]<<24|m.onchip[0xFE95]<<16|m.onchip[0xFE96]<<8|m.onchip[0xFE97], m.onchip[0xFEA0]<<24|m.onchip[0xFEA1]<<16|m.onchip[0xFEA2]<<8|m.onchip[0xFEA3], m.onchip[0xFEAC]<<24|m.onchip[0xFEAD]<<16|m.onchip[0xFEAE]<<8|m.onchip[0xFEAF], m.onchip[0xFEE2]<<8|m.onchip[0xFEE3]);
    printf("saves: nand written=%d, eeprom written=%d\n", m.nand.dirty, m.rtc.dirty);
    printf("blitter: %llu draws, %llu uploads, %llu bad ops\n", (unsigned long long)g_blit.draws, (unsigned long long)g_blit.uploads, (unsigned long long)g_blit.bad_ops);
    printf("%llu frames in %.2f s (%.0f fps), %llu instructions\n", (unsigned long long)frames, secs, frames / secs,
           (unsigned long long)s->insns);
    if (!interp) { dispatch_report(); if (misses) dispatch_write_misses(misses); }
    return 0;
}
