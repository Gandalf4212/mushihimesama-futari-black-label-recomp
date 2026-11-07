/* C translator (discover + gen_c in C), see recomp.c. GPL-2.0-or-later. */
#ifndef RECOMP_H
#define RECOMP_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint32_t main_entry, vbr;          /* game.json boot.main_entry, boot.vbr */
    uint32_t vector_addr, vector_size; /* boot.vector_table (size 0 = none) */
    uint32_t code_end;                 /* boot.code_end (0 = 0x0C300000) */
    const char *extra_path;            /* hints file: one hex address per line, '#' comments (NULL = none) */
    int per_file;                      /* functions per .c file (0 = 150) */
} RecompCfg;
/* Writes outdir/recomp_NNN.c + recomp_table.c from a RAM image (after the boot code ran).
 * Returns 1 (msg: summary) or 0 (msg: error). */
int recomp_translate(const uint8_t *ram, uint32_t ram_len, const RecompCfg *c, const char *outdir,
                     int *nfiles, char *msg, size_t mn);
#endif
