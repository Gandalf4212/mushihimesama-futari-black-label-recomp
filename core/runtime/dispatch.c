/* Dispatcher: runs recompiled code, falling back to the interpreter for unknown addresses.
 * GPL-2.0-or-later. */
#include "dispatch.h"
#include <stdio.h>
#include <stdlib.h>

extern const struct { uint32_t addr; void (*fn)(SH3 *); } recomp_table[];
extern const unsigned recomp_table_count;

static recomp_fn *direct;          /* indexed by (phys - RAM_BASE) / 2 */
static uint32_t direct_size;
uint64_t dispatch_misses, dispatch_calls;

#define RAM_BASE 0x0C000000u
#define MISS_LOG 4096
static uint32_t miss_pc[MISS_LOG]; static uint64_t miss_n[MISS_LOG]; static int miss_used;

void dispatch_init(uint32_t ram_size)
{
    direct_size = ram_size / 2;
    direct = calloc(direct_size, sizeof *direct);
    for (unsigned i = 0; i < recomp_table_count; i++) {
        uint32_t o = recomp_table[i].addr - RAM_BASE;
        if (o / 2 < direct_size) direct[o / 2] = recomp_table[i].fn;
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
    for (int i = 0; i < miss_used; i++) if (miss_pc[i] == pc) { miss_n[i]++; return; }
    if (miss_used < MISS_LOG) { miss_pc[miss_used] = pc; miss_n[miss_used++] = 1; }
}

/* Called by generated code when entered at an address it has no label for. */
void sh3_miss(SH3 *s) { note_miss(s->pc); sh3_step(s); }
void sh3_miss_at(SH3 *s, uint32_t pc) { s->pc = pc; note_miss(pc); sh3_step(s); }
void sh3_bad_slot(SH3 *s, uint32_t pc) { fprintf(stderr, "illegal delay slot at %08x\n", pc); abort(); }

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
