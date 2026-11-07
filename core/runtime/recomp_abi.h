/* ABI between the runtime (futaribl.exe, ours) and the game code compiled on the player's PC (game.dll).
 * The generated C calls six runtime helpers; inside the DLL they are thin forwarders (recomp_dll.c) to the
 * host table passed in at load time, so the generated C is the same for static and DLL builds.
 * Bump RECOMP_ABI whenever SH3 (sh3.h), these structs, or what gen_c.py emits changes: the exe then
 * refuses an old game.dll and asks for a rebuild. GPL-2.0-or-later. */
#ifndef RECOMP_ABI_H
#define RECOMP_ABI_H
#include "../sh3/sh3.h"

#define RECOMP_ABI 1u

typedef struct { uint32_t addr; void (*fn)(SH3 *); } RecompEntry;

typedef struct {
    uint32_t abi, size;                       /* RECOMP_ABI, sizeof(RecompHost) */
    void (*miss)(SH3 *s);
    void (*miss_at)(SH3 *s, uint32_t pc);
    void (*bad_slot)(SH3 *s, uint32_t pc);
    void (*set_sr)(SH3 *s, uint32_t v);
    void (*div1)(SH3 *s, uint32_t *rn, uint32_t rm);
    void (*macl)(SH3 *s, int n, int m);
} RecompHost;

typedef struct {
    uint32_t abi;                             /* RECOMP_ABI the DLL was built with */
    uint32_t sh3_size;                        /* sizeof(SH3) in the DLL */
    uint32_t u4_crc;                          /* CRC32 of the program ROM it was generated from */
    uint32_t count;                           /* entries in table */
    const RecompEntry *table;
} RecompGame;

/* Exported by game.dll. Returns NULL if host->abi / host->size don't match. */
typedef const RecompGame *(*recomp_dll_init_fn)(const RecompHost *host);
#define RECOMP_DLL_INIT "recomp_dll_init"
#endif
