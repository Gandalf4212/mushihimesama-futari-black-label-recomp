/* Dispatcher: runs recompiled code, falling back to the interpreter for unknown addresses.
 * GPL-2.0-or-later. */
#include "dispatch.h"
#include <stdio.h>
#include <stdlib.h>

static recomp_fn *direct;          /* indexed by (phys - RAM_BASE) / 2 */
static uint32_t direct_size;
uint64_t dispatch_misses, dispatch_calls;

#define RAM_BASE 0x0C000000u
#define MISS_LOG 65536                 /* power of two: open-addressing hash, kept at most half full */
static uint32_t miss_pc[MISS_LOG]; static uint64_t miss_n[MISS_LOG]; static int miss_used;
static int32_t miss_slot[MISS_LOG * 2];   /* pc hash -> index+1 into miss_pc, 0 = empty */

void dispatch_init(uint32_t ram_size, const RecompEntry *table, unsigned count)
{
    direct_size = ram_size / 2;
    direct = calloc(direct_size, sizeof *direct);
    for (unsigned i = 0; table && i < count; i++) {   /* no table: everything runs on the interpreter */
        uint32_t o = table[i].addr - RAM_BASE;
        if (o / 2 < direct_size) direct[o / 2] = table[i].fn;
    }
}

static recomp_fn lookup(uint32_t pc)
{
    uint32_t o = (pc & 0x1FFFFFFFu) - RAM_BASE;
    return o / 2 < direct_size && !(o & 1) ? direct[o / 2] : NULL;
}

static void note_miss(uint32_t pc)
{
    dispatch_misses++;
    uint32_t h = (pc * 2654435761u) >> 15;                 /* 17-bit hash */
    for (;; h = (h + 1) & (MISS_LOG * 2 - 1)) {
        int32_t k = miss_slot[h];
        if (!k) break;
        if (miss_pc[k - 1] == pc) { miss_n[k - 1]++; return; }
    }
    if (miss_used < MISS_LOG) { miss_pc[miss_used] = pc; miss_n[miss_used] = 1; miss_slot[h] = ++miss_used; }
}

/* Called by generated code when entered at an address it has no label for. */
void sh3_miss(SH3 *s) { note_miss(s->pc); sh3_step(s); }
void sh3_miss_at(SH3 *s, uint32_t pc) { s->pc = pc; note_miss(pc); sh3_step(s); }
void sh3_bad_slot(SH3 *s, uint32_t pc) { fprintf(stderr, "illegal delay slot at %08x\n", pc); abort(); }

/* The helpers game.dll forwards to (recomp_abi.h). */
const RecompHost *dispatch_host(void)
{
    static const RecompHost h = { RECOMP_ABI, sizeof(RecompHost), sh3_miss, sh3_miss_at, sh3_bad_slot, sh3_set_sr, sh3_div1, sh3_macl };
    return &h;
}

/* One dispatch: run recompiled code from s->pc until it returns (call/return/jump/event). */
void dispatch_step(SH3 *s)
{
    recomp_fn f = lookup(s->pc);
    dispatch_calls++;
    if (f) f(s);
    else { note_miss(s->pc); sh3_step(s); }
}

void dispatch_run(SH3 *s, uint64_t until_cycles, uint32_t stop_pc)
{
    s->event_at = until_cycles;
    while (s->cycles < until_cycles && !s->trap && !s->sleeping) {
        if (stop_pc && (s->pc & 0x1FFFFFFFu) == (stop_pc & 0x1FFFFFFFu)) break;
        recomp_fn f = lookup(s->pc);
        dispatch_calls++;
        if (f) f(s);
        else { note_miss(s->pc); sh3_step(s); }   /* interpreter fallback, one instruction */
    }
}

void dispatch_write_misses(const char *path)
{
    FILE *f = fopen(path, "w"); if (!f) return;
    for (int i = 0; i < miss_used; i++) fprintf(f, "%08X\n", miss_pc[i] & 0x1FFFFFFFu);
    fclose(f);
}

void dispatch_report(void)
{
    printf("dispatch: %llu calls, %llu interpreter fallbacks at %d addresses\n",
           (unsigned long long)dispatch_calls, (unsigned long long)dispatch_misses, miss_used);
    for (int i = 0; i < miss_used && i < 20; i++) printf("  miss %08x x%llu\n", miss_pc[i], (unsigned long long)miss_n[i]);
}
