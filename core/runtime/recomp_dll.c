/* Linked into game.dll with the generated code (build/<set>/gen/recomp_*.c). Provides the runtime helpers
 * the generated code calls, forwarding them to the exe, and exports recomp_dll_init. Build with
 * -DRECOMP_U4_CRC=0x........ (the program ROM's CRC). GPL-2.0-or-later. */
#include "recomp_abi.h"
#include <stddef.h>

extern const RecompEntry recomp_table[];       /* gen_c.py: same layout as RecompEntry */
extern const unsigned recomp_table_count;

static const RecompHost *H;

void sh3_miss(SH3 *s) { H->miss(s); }
void sh3_miss_at(SH3 *s, uint32_t pc) { H->miss_at(s, pc); }
void sh3_bad_slot(SH3 *s, uint32_t pc) { H->bad_slot(s, pc); }
void sh3_set_sr(SH3 *s, uint32_t v) { H->set_sr(s, v); }
void sh3_div1(SH3 *s, uint32_t *rn, uint32_t rm) { H->div1(s, rn, rm); }
void sh3_macl(SH3 *s, int n, int m) { H->macl(s, n, m); }

__declspec(dllexport) const RecompGame *recomp_dll_init(const RecompHost *host)
{
    static RecompGame g;
    if (!host || host->abi != RECOMP_ABI || host->size != sizeof(RecompHost)) return NULL;
    H = host;
    g.abi = RECOMP_ABI; g.sh3_size = sizeof(SH3); g.u4_crc = RECOMP_U4_CRC;
    g.count = recomp_table_count; g.table = recomp_table;
    return &g;
}
