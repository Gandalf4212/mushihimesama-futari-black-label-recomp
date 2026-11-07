/* Game code loader. Windows: the recompiled game code lives in game.dll, compiled on the player's PC
 * (docs/builder-plan.md); the exe itself contains none of it. Other targets: linked statically.
 * GPL-2.0-or-later. */
#include "gamedll.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>

static const char *try_load(const char *path, const RecompEntry **t, unsigned *n, char *why, size_t wn)
{
    HMODULE h = LoadLibraryA(path);
    if (!h) {
        FILE *f = fopen(path, "rb");
        if (!f) return "missing";
        fclose(f); snprintf(why, wn, "%s: can't be loaded (damaged or blocked)", path); return why;
    }
    recomp_dll_init_fn init = (recomp_dll_init_fn)(void *)GetProcAddress(h, RECOMP_DLL_INIT);
    const RecompGame *g = init ? init(dispatch_host()) : NULL;
    const char *bad = !init ? "not a game code DLL"
                    : !g || g->abi != RECOMP_ABI ? "built by an older version"
                    : g->sh3_size != sizeof(SH3) ? "built by an older version (CPU state)"
                    : g->u4_crc != GAME_U4_CRC ? "built from a different program ROM" : NULL;
    if (bad) { FreeLibrary(h); snprintf(why, wn, "%s: %s", path, bad); return bad; }
    *t = g->table; *n = g->count;
    return NULL;
}

int game_code_load(const RecompEntry **table, unsigned *count, char *why, size_t wn)
{
    *table = NULL; *count = 0; why[0] = 0;
    static const char *paths[] = { "assets/game.dll", "game.dll" };   /* builder output; test builds ship it next to the exe */
    for (int i = 0; i < 2; i++) {
        char w[256] = "";
        const char *r = try_load(paths[i], table, count, w, sizeof w);
        if (!r) return 1;
        if (w[0] && !why[0]) snprintf(why, wn, "%s", w);   /* report the first DLL that exists but is wrong */
    }
    if (!why[0]) snprintf(why, wn, "assets/game.dll not found");
    return 0;
}
#else
int game_code_load(const RecompEntry **table, unsigned *count, char *why, size_t wn)
{
    *table = recomp_table; *count = recomp_table_count; why[0] = 0; (void)wn;
    return 1;
}
#endif
