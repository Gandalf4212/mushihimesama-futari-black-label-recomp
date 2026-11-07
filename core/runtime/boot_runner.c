/* Boot runner: executes a CV1000 game's own startup code on the reference interpreter until it
 * reaches the main entry point, then writes the RAM image (input for the recompiler).
 * Usage: boot_runner <u4 rom (as dumped)> <ram_size hex> <main_entry hex> <out ram.bin> [max_insns]
 * GPL-2.0-or-later. */
#include "../sh3/sh3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t addr; uint64_t reads, writes; uint32_t last; } IoStat;

typedef struct {
    uint8_t *rom; uint32_t rom_size;
    uint8_t *ram; uint32_t ram_size;
    uint8_t onchip[0x10000];   /* 0xFFFF0000-0xFFFFFFFF */
    uint8_t area1[0x10000];    /* 0x04000000-0x0400FFFF (ports etc, via 0xA4000000) */
    IoStat io[256]; int nio;
} Bus;

static void io_note(Bus *b, uint32_t a, int w, uint32_t v)
{
    for (int i = 0; i < b->nio; i++)
        if (b->io[i].addr == a) { if (w) b->io[i].writes++; else b->io[i].reads++; b->io[i].last = v; return; }
    if (b->nio < 256) { b->io[b->nio] = (IoStat){a, !w, w, v}; b->nio++; }
}

/* Returns pointer for plain memory, or NULL for device space. */
static uint8_t *map(Bus *b, uint32_t a)
{
    if (a >= 0xFFFF0000u) return &b->onchip[a & 0xffff];
    if (a >= 0xE0000000u) return NULL;            /* cache arrays etc. */
    uint32_t p = a & 0x1fffffffu;
    if (p < 0x00400000u) return &b->rom[p % b->rom_size];
    if (p >= 0x0C000000u && p < 0x10000000u) return &b->ram[(p - 0x0C000000u) & (b->ram_size - 1)];
    if (p >= 0x04000000u && p < 0x04010000u) return &b->area1[p & 0xffff];
    return NULL;
}

static uint32_t dev_read(Bus *b, uint32_t a, int size)
{
    uint32_t p = a & 0x1fffffffu, v = 0;
    if (p >= 0x10000000u && p < 0x10000008u) v = 0xff;   /* NAND data (not needed for boot yet) */
    io_note(b, a, 0, v); (void)size;
    return v;
}
static void dev_write(Bus *b, uint32_t a, uint32_t v, int size) { io_note(b, a, 1, v); (void)size; }

/* port data registers: inputs inactive (high), NAND ready */
static int port_read(uint32_t a, uint32_t *v)
{
    uint32_t p = a & 0x1fffffffu;
    if (p >= 0x04000120u && p <= 0x04000135u) { *v = 0xff; return 1; }
    return 0;
}

static uint8_t rb(void *c, uint32_t a) { Bus *b = c; uint32_t v; if (port_read(a, &v)) return v; uint8_t *m = map(b, a); return m ? *m : dev_read(b, a, 1); }
static uint16_t rw(void *c, uint32_t a) { Bus *b = c; uint32_t v; if (port_read(a, &v)) return 0xffff; uint8_t *m = map(b, a); return m ? (uint16_t)(m[0] << 8 | m[1]) : dev_read(b, a, 2); }
static uint32_t rl(void *c, uint32_t a) { Bus *b = c; uint8_t *m = map(b, a); return m ? (uint32_t)m[0] << 24 | m[1] << 16 | m[2] << 8 | m[3] : dev_read(b, a, 4); }
static void wb(void *c, uint32_t a, uint8_t v) { Bus *b = c; uint8_t *m = map(b, a); if (m && (a & 0x1fffffffu) >= 0x00400000u) *m = v; else if (!m) dev_write(b, a, v, 1); }
static void ww(void *c, uint32_t a, uint16_t v) { Bus *b = c; uint8_t *m = map(b, a); if (m && (a & 0x1fffffffu) >= 0x00400000u) { m[0] = v >> 8; m[1] = v; } else if (!m) dev_write(b, a, v, 2); }
static void wl(void *c, uint32_t a, uint32_t v) { Bus *b = c; uint8_t *m = map(b, a); if (m && (a & 0x1fffffffu) >= 0x00400000u) { m[0] = v >> 24; m[1] = v >> 16; m[2] = v >> 8; m[3] = v; } else if (!m) dev_write(b, a, v, 4); }

/* For the first-run builder: boots the program ROM (as dumped) and fills ram_out (ram_size bytes) with the
 * RAM image at the main entry. Returns 1 if the entry was reached. */
static int boot_run_image(const uint8_t *rom, uint32_t rom_size, uint32_t ram_size, uint32_t entry, uint8_t *ram_out)
{
    static Bus b;
    memset(&b, 0, sizeof b);
    b.rom_size = rom_size; b.rom = malloc(rom_size);
    for (uint32_t i = 0; i + 1 < rom_size; i += 2) { b.rom[i] = rom[i + 1]; b.rom[i + 1] = rom[i]; }   /* word swap */
    b.ram_size = ram_size; b.ram = ram_out; memset(ram_out, 0, ram_size);
    SH3 s; memset(&s, 0, sizeof s);
    s.bus = (SH3Bus){&b, rb, rw, rl, wb, ww, wl};
    sh3_reset(&s);
    while (s.insns < 2000000000ull && (s.pc & 0x1fffffffu) != (entry & 0x1fffffffu) && !s.trap && !s.sleeping) sh3_step(&s);
    free(b.rom);
    return (s.pc & 0x1fffffffu) == (entry & 0x1fffffffu);
}

#ifndef BOOT_RUNNER_NO_MAIN
int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s u4 ram_size main_entry out.bin [max_insns]\n", argv[0]); return 2; }
    static Bus b;
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); b.rom_size = ftell(f); fseek(f, 0, SEEK_SET);
    b.rom = malloc(b.rom_size);
    if (fread(b.rom, 1, b.rom_size, f) != b.rom_size) return 1;
    fclose(f);
    for (uint32_t i = 0; i < b.rom_size; i += 2) { uint8_t t = b.rom[i]; b.rom[i] = b.rom[i + 1]; b.rom[i + 1] = t; } /* word swap */
    b.ram_size = strtoul(argv[2], 0, 16); b.ram = calloc(1, b.ram_size);
    uint32_t entry = strtoul(argv[3], 0, 16);
    uint64_t max = argc > 5 ? strtoull(argv[5], 0, 10) : 2000000000ull;

    SH3 s; memset(&s, 0, sizeof s);
    s.bus = (SH3Bus){&b, rb, rw, rl, wb, ww, wl};
    sh3_reset(&s);
    uint32_t hist[16] = {0}; int hp = 0;
    while (s.insns < max) {
        if ((s.pc & 0x1fffffffu) == (entry & 0x1fffffffu)) break;
        hist[hp++ & 15] = s.pc;
        sh3_step(&s);
        if (s.trap || s.sleeping) { fprintf(stderr, "stopped: %s at %08x\n", s.trap ? "trapa" : "sleep", s.pc); break; }
    }
    int reached = (s.pc & 0x1fffffffu) == (entry & 0x1fffffffu);
    printf("%s after %llu instructions, pc=%08x\n", reached ? "REACHED main entry" : "did NOT reach entry",
           (unsigned long long)s.insns, s.pc);
    if (!reached) { printf("recent pcs:"); for (int i = 0; i < 16; i++) printf(" %08x", hist[(hp + i) & 15]); printf("\n"); }
    printf("device accesses (%d addresses):\n", b.nio);
    for (int i = 0; i < b.nio && i < 40; i++)
        printf("  %08x r%llu w%llu last=%x\n", b.io[i].addr, (unsigned long long)b.io[i].reads, (unsigned long long)b.io[i].writes, b.io[i].last);
    printf("regs:"); for (int i = 0; i < 16; i++) printf(" r%d=%08x", i, s.r[i]); printf(" sr=%08x vbr=%08x pr=%08x\n", s.sr, s.vbr, s.pr);
    if (reached) {
        FILE *o = fopen(argv[4], "wb"); fwrite(b.ram, 1, b.ram_size, o); fclose(o);
        printf("wrote %s\n", argv[4]);
    }
    return reached ? 0 : 1;
}
#endif
