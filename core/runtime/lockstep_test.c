/* Lockstep test: generated code vs reference interpreter from the same post-boot state.
 * Usage: lockstep_test <u4> <ram_size hex> <main_entry hex> <cycles> [stop_pc hex]
 * Runs recompiled code for <cycles> (block granularity), then runs the interpreter to the exact same
 * instruction count, and compares all registers and RAM. GPL-2.0-or-later. */
#define BOOT_RUNNER_NO_MAIN
#include "boot_runner.c"
#include "dispatch.h"

static uint64_t fnv(const uint8_t *p, size_t n) { uint64_t h = 1469598103934665603ull; while (n--) h = (h ^ *p++) * 1099511628211ull; return h; }

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s u4 ram_size main_entry cycles [stop_pc]\n", argv[0]); return 2; }
    static Bus a, b;
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); a.rom_size = ftell(f); fseek(f, 0, SEEK_SET);
    a.rom = malloc(a.rom_size); if (fread(a.rom, 1, a.rom_size, f) != a.rom_size) return 1; fclose(f);
    for (uint32_t i = 0; i < a.rom_size; i += 2) { uint8_t t = a.rom[i]; a.rom[i] = a.rom[i + 1]; a.rom[i + 1] = t; }
    a.ram_size = strtoul(argv[2], 0, 16); a.ram = calloc(1, a.ram_size);
    uint32_t entry = strtoul(argv[3], 0, 16);
    uint64_t cycles = strtoull(argv[4], 0, 10);
    uint32_t stop = argc > 5 ? strtoul(argv[5], 0, 16) : 0;

    SH3 sa; memset(&sa, 0, sizeof sa); sa.bus = (SH3Bus){&a, rb, rw, rl, wb, ww, wl}; sh3_reset(&sa);
    while ((sa.pc & 0x1fffffffu) != (entry & 0x1fffffffu)) sh3_step(&sa);   /* boot on interpreter */

    b = a; b.ram = malloc(a.ram_size); memcpy(b.ram, a.ram, a.ram_size);   /* clone machine */
    SH3 sb = sa; sb.bus.ctx = &b;
    uint64_t c0 = sa.cycles;

    dispatch_init(b.ram_size);
    dispatch_run(&sb, c0 + cycles, stop);                                   /* recompiled */
    while (sa.cycles < sb.cycles) sh3_step(&sa);                             /* reference */

    int bad = 0;
    for (int i = 0; i < 16; i++) if (sa.r[i] != sb.r[i]) { printf("r%d: interp %08x recomp %08x\n", i, sa.r[i], sb.r[i]); bad = 1; }
    #define CMP(x) if (sa.x != sb.x) { printf(#x ": interp %08x recomp %08x\n", sa.x, sb.x); bad = 1; }
    CMP(pc) CMP(sr) CMP(gbr) CMP(vbr) CMP(pr) CMP(mach) CMP(macl) CMP(ssr) CMP(spc)
    uint64_t ha = fnv(a.ram, a.ram_size), hb = fnv(b.ram, b.ram_size);
    if (ha != hb) {
        bad = 1;
        for (uint32_t o = 0; o < a.ram_size; o++) if (a.ram[o] != b.ram[o]) { printf("first RAM diff at %08x\n", 0x0C000000u + o); break; }
    }
    printf("ran %llu instructions after boot; pc=%08x; %s\n", (unsigned long long)(sb.cycles - c0), sb.pc,
           bad ? "MISMATCH" : "MATCH (registers + all RAM)");
    dispatch_report();
    return bad;
}
