/* C translator: static code discovery + C generation over a CV1000 RAM image.
 * A port of discover.py + gen_c.py (same algorithm, same work order, byte-identical output), so the
 * player's PC can generate the game code from their own ROM (docs/builder-plan.md). The instruction
 * table comes from isa.py through gen_isa_c.py (isa_table.inc). GPL-2.0-or-later.
 *
 * CLI (with -DRECOMP_MAIN):
 *   recomp <ram.bin> <out_dir> --entry ADDR --vbr ADDR [--vectors ADDR SIZE] [--code-end ADDR]
 *          [--extra hints.txt] [--per-file N]
 */
#include "recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "isa_table.inc"

#define RAM_BASE 0x0C000000u
#define PHYS(a) ((uint32_t)(a) & 0x1FFFFFFFu)

static const uint8_t *D; static uint32_t D_len, D_end;
static int16_t TABLE[65536];

static int ok(uint32_t a) { return a >= RAM_BASE && a < D_end - 1 && !(a & 1); }
static uint16_t w16(uint32_t a) { uint32_t o = a - RAM_BASE; return (uint16_t)(D[o] << 8 | D[o + 1]); }
static uint32_t w32(uint32_t a) { uint32_t o = a - RAM_BASE; return (uint32_t)D[o] << 24 | D[o + 1] << 16 | D[o + 2] << 8 | D[o + 3]; }
static uint32_t slot_of(uint32_t a) { return (a - RAM_BASE) >> 1; }

static void build_table(void)
{
    for (int op = 0; op < 65536; op++) TABLE[op] = -1;
    for (int i = 0; i < ISA_COUNT; i++)
        for (int op = 0; op < 65536; op++)
            if ((op & ISA[i].fixed) == ISA[i].val) TABLE[op] = (int16_t)i;
}

static int starts(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }
static int ends_rn(const char *s) { size_t n = strlen(s); return n >= 2 && !strcmp(s + n - 2, "Rn"); }

static uint32_t static_target(int kind, uint16_t op, uint32_t pc)
{
    if (kind == K_CBR || kind == K_CBRS) return pc + 4 + (uint32_t)((int32_t)(int8_t)(op & 0xFF) * 2);
    return pc + 4 + (uint32_t)((((int32_t)(op & 0xFFF) ^ 0x800) - 0x800) * 2);   /* br, call */
}

/* ---------- growable arrays ---------- */
typedef struct { uint32_t *v; size_t n, cap; } Vec;
static void push(Vec *a, uint32_t x) { if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 64; a->v = realloc(a->v, a->cap * sizeof *a->v); } a->v[a->n++] = x; }
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }

/* ---------- discovery (discover.py) ---------- */
static uint8_t *literals, *done, *rejected, *covered;   /* one byte per halfword slot */
static uint32_t *seen_gen, *block_gen, gen;                      /* per-walk sets by generation stamp */
static uint32_t nslots;

static void lit_add(uint32_t v) { v = PHYS(v); if (v >= RAM_BASE && slot_of(v) < nslots && !(v & 1)) literals[slot_of(v)] = 1; }

/* Returns 1 with sorted blocks, or 0 if strict and an illegal opcode was reached. Calls go to calls
 * (in first-found order; order doesn't change the result). seen slots get marked in seen_gen. */
static int walk_function(uint32_t entry, Vec *calls, int strict, Vec *blocks, Vec *seenlist)
{
    Vec work = {0};
    gen++;
    blocks->n = 0; seenlist->n = 0;
#define BLOCK(t) do { uint32_t t_ = (t); if (block_gen[slot_of(t_)] != gen) { block_gen[slot_of(t_)] = gen; push(blocks, t_); } } while (0)
#define SEE(a) do { uint32_t a_ = (a); if (a_ >= RAM_BASE && slot_of(a_) < nslots && seen_gen[slot_of(a_)] != gen) { seen_gen[slot_of(a_)] = gen; push(seenlist, a_); } } while (0)
    BLOCK(entry); push(&work, entry);
    while (work.n) {
        uint32_t pc = work.v[--work.n];
        int has[16] = {0}; uint32_t lit[16];
        for (;;) {
            if (!ok(pc) || seen_gen[slot_of(pc)] == gen) break;
            SEE(pc);
            uint16_t op = w16(pc);
            int idx = TABLE[op];
            if (idx < 0) { if (strict) { free(work.v); return 0; } break; }
            const IsaEntry *e = &ISA[idx];
            int rn = (op >> 8) & 15;
            if (!strcmp(e->mn, "mov.l @(disp,PC),Rn")) {
                uint32_t a = ((pc + 4) & ~3u) + (op & 0xFFu) * 4;
                if (ok(a)) { has[rn] = 1; lit[rn] = w32(a); lit_add(w32(a)); }
            } else if (e->kind == K_OP && has[rn] && ends_rn(e->mn)) has[rn] = 0;
            if (e->kind == K_OP) { pc += 2; continue; }
            uint32_t tgt = (e->kind == K_CBR || e->kind == K_CBRS || e->kind == K_BR || e->kind == K_CALL) ? static_target(e->kind, op, pc) : 0;
            uint32_t slot = pc + 2;
            if (e->kind == K_CBR || e->kind == K_CBRS) {
                uint32_t ts[2] = { PHYS(tgt), PHYS(e->kind == K_CBR ? pc + 2 : pc + 4) };
                for (int k = 0; k < 2; k++) if (ok(ts[k])) { BLOCK(ts[k]); push(&work, ts[k]); }
                if (e->kind == K_CBRS) SEE(slot);
                break;
            }
            SEE(slot);
            if (e->kind == K_BR) { uint32_t t = PHYS(tgt); if (ok(t)) { BLOCK(t); push(&work, t); } break; }
            if (e->kind == K_CALL || e->kind == K_ICALL) {
                if (e->kind == K_CALL) push(calls, PHYS(tgt));
                else if (starts(e->mn, "jsr") && has[rn] && ok(PHYS(lit[rn]))) push(calls, PHYS(lit[rn]));
                uint32_t ret = pc + 4; BLOCK(ret); push(&work, ret);
                break;
            }
            if (e->kind == K_IJMP) {
                if (starts(e->mn, "jmp") && has[rn] && ok(PHYS(lit[rn]))) push(calls, PHYS(lit[rn]));
                break;
            }
            break;   /* ret, rte, trap, sleep */
        }
    }
    free(work.v);
    qsort(blocks->v, blocks->n, sizeof *blocks->v, cmp_u32);
    return 1;
#undef BLOCK
#undef SEE
}

typedef struct { uint32_t entry; uint32_t *blocks; uint32_t n; } Func;
typedef struct { uint32_t addr; int strict; } Work;

static int cmp_func(const void *a, const void *b) { return cmp_u32(&((const Func *)a)->entry, &((const Func *)b)->entry); }

static Func *discover(const RecompCfg *c, size_t *nfunc, char *err, size_t en)
{
    Vec entries = {0};
    push(&entries, PHYS(c->main_entry));
    uint32_t vbr = PHYS(c->vbr);
    push(&entries, vbr + 0x100); push(&entries, vbr + 0x400); push(&entries, vbr + 0x600);
    if (c->vector_size) {
        uint32_t a = PHYS(c->vector_addr);
        for (uint32_t k = 0; k < c->vector_size / 4; k++) { uint32_t v = PHYS(w32(a + 4 * k)); if (ok(v)) push(&entries, v); }
    }
    if (c->extra_path) {
        FILE *f = fopen(c->extra_path, "r");
        if (!f) { snprintf(err, en, "can't open %s", c->extra_path); free(entries.v); return NULL; }
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char *h = strchr(line, '#'); if (h) *h = 0;
            char *p = line; while (*p == ' ' || *p == '\t') p++;
            if (*p && *p != '\n' && *p != '\r') push(&entries, PHYS(strtoul(p, NULL, 16)));
        }
        fclose(f);
    }
    /* pointer scan: aligned words anywhere in the image that point into the code region */
    uint32_t code_hi = PHYS(c->code_end ? c->code_end : 0x0C300000u);
    for (uint32_t o = 0; o + 3 < D_len; o += 4) {
        uint32_t v = PHYS((uint32_t)D[o] << 24 | D[o + 1] << 16 | D[o + 2] << 8 | D[o + 3]);
        if (v >= RAM_BASE && v < code_hi && !(v & 1) && slot_of(v) < nslots) literals[slot_of(v)] = 1;
    }
    /* work = sorted unique entries, popped from the end */
    qsort(entries.v, entries.n, sizeof *entries.v, cmp_u32);
    Work *work = NULL; size_t wn = 0, wcap = 0;
#define WPUSH(a, s) do { if (wn == wcap) { wcap = wcap ? wcap * 2 : 1024; work = realloc(work, wcap * sizeof *work); } work[wn].addr = (a); work[wn].strict = (s); wn++; } while (0)
    for (size_t i = 0; i < entries.n; i++) if (!i || entries.v[i] != entries.v[i - 1]) WPUSH(entries.v[i], 0);
    free(entries.v);
    Func *fn = NULL; size_t fnn = 0, fcap = 0;
    Vec calls = {0}, blocks = {0}, seenl = {0};
    for (;;) {
        while (wn) {
            Work w = work[--wn];
            if (!ok(w.addr) || done[slot_of(w.addr)]) continue;
            calls.n = 0;
            if (!walk_function(w.addr, &calls, w.strict, &blocks, &seenl)) { rejected[slot_of(w.addr)] = 1; continue; }
            done[slot_of(w.addr)] = 1;
            if (fnn == fcap) { fcap = fcap ? fcap * 2 : 1024; fn = realloc(fn, fcap * sizeof *fn); }
            fn[fnn].entry = w.addr; fn[fnn].n = (uint32_t)blocks.n;
            fn[fnn].blocks = malloc(blocks.n * sizeof(uint32_t)); memcpy(fn[fnn].blocks, blocks.v, blocks.n * sizeof(uint32_t)); fnn++;
            for (size_t i = 0; i < seenl.n; i++) covered[slot_of(seenl.v[i])] = 1;
            for (size_t i = 0; i < calls.n; i++) {
                uint32_t a = calls.v[i];
                if (!(a >= RAM_BASE && slot_of(a) < nslots && done[slot_of(a)])) WPUSH(a, 0);
            }
        }
        /* literal-pool values not yet decoded: pointers into decoded instructions are certain starts,
           others must decode cleanly (strict) */
        for (uint32_t s = 0; s < nslots; s++) {
            if (!literals[s] || done[s] || rejected[s]) continue;
            uint32_t v = RAM_BASE + s * 2;
            if (ok(v)) WPUSH(v, !covered[s]);
        }
        if (!wn) break;
    }
#undef WPUSH
    free(work); free(calls.v); free(blocks.v); free(seenl.v);
    qsort(fn, fnn, sizeof *fn, cmp_func);
    *nfunc = fnn;
    return fn;
}

/* ---------- C generation (gen_c.py) ---------- */
static int base_cycles(uint16_t op)
{
    int s = op >> 12;
    if (s == 0) {
        int lo8 = op & 0xFF;
        if (lo8 == 0x0B || lo8 == 0x23) return 2;
        if (lo8 == 0x1B) return 3;
        if (lo8 == 0x2B) return 4;
        if ((op & 0xF) == 7) return 2;
        if ((op & 0xF) == 0xF) return 3;
        return 1;
    }
    if (s == 2) return ((op & 0xF) == 0xE || (op & 0xF) == 0xF) ? 2 : 1;
    if (s == 3) return ((op & 0xF) == 5 || (op & 0xF) == 0xD) ? 2 : 1;
    if (s == 4) {
        if ((op & 0xF) == 0xF) return 3;
        switch (op & 0xFF) { case 0x03: return 2; case 0x07: return 3; case 0x1B: return 4; default: return 1; }
    }
    if (s == 8) { int k = (op >> 8) & 0xF; return (k == 9 || k == 0xB) ? 3 : (k == 0xD || k == 0xF) ? 2 : 1; }
    if (s == 0xA || s == 0xB) return 2;
    if (s == 0xC) return ((op >> 8) & 0xF) == 3 ? 8 : 1;
    return 1;
}

static int is_checkints(uint16_t op) { return op >> 12 == 4 && ((op & 0xFF) == 0x0E || (op & 0xFF) == 0x07); }

static int is_flush(uint16_t op)
{
    int g = op >> 12, lo = op & 0xF, lo8 = op & 0xFF;
    if (g == 0) return lo == 4 || lo == 5 || lo == 6 || lo == 0xC || lo == 0xD || lo == 0xE;
    if (g == 1 || g == 5 || g == 9 || g == 0xD) return 1;
    if (g == 2 || g == 6) return lo <= 2 || (4 <= lo && lo <= 6);
    if (g == 4) { static const int l[] = { 0x02, 0x12, 0x22, 0x03, 0x13, 0x23, 0x06, 0x16, 0x26, 0x07, 0x17, 0x27, 0x1B };
                  for (unsigned i = 0; i < sizeof l / sizeof *l; i++) { if (lo8 == l[i]) return 1; }
                  return 0; }
    if (g == 8) { int k = (op >> 8) & 0xF; return k == 0 || k == 1 || k == 4 || k == 5; }
    if (g == 0xC) { int k = (op >> 8) & 0xF; return k <= 2 || (4 <= k && k <= 6); }
    return 0;
}

typedef struct { char *p; size_t n, cap; } Buf;
static void bput(Buf *b, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int k = vsnprintf(b->p ? b->p + b->n : NULL, b->p ? b->cap - b->n : 0, fmt, ap);
        va_end(ap);
        if (k >= 0 && b->p && b->n + (size_t)k < b->cap) { b->n += (size_t)k; return; }
        b->cap = (b->cap + (size_t)k + 1) * 2; b->p = realloc(b->p, b->cap);
    }
}

static void fields(Buf *b, const IsaEntry *e, uint16_t op)
{
    int first = 1;
    for (int f = 0; f < 4; f++) {
        if (e->sh[f] < 0) continue;
        uint32_t v = (op >> e->sh[f]) & e->mask[f];
        if (!first) bput(b, " ");
        first = 0;
        if (f < 2) bput(b, "const int %c = %u; (void)%c;", "nm"[f], v, "nm"[f]);
        else if (f == 2) bput(b, "const uint32_t IMM = %uu; const int32_t SIMM = %d; (void)IMM; (void)SIMM;", v, (v & 0x80) ? (int)v - 256 : (int)v);
        else { const char *nm = e->mask[f] == 15 ? "D4" : e->mask[f] == 255 ? "D8" : "D12"; bput(b, "const uint32_t %s = %uu; (void)%s;", nm, v, nm); }
    }
}

static void body_slot(Buf *b, uint16_t op, uint32_t pc, int in_slot)
{
    const IsaEntry *e = &ISA[TABLE[op]];
    bput(b, "{ const uint32_t PC = hi | 0x%08Xu; (void)PC; SH3_FETCH(0x%04Xu, PC); ", pc, op);
    fields(b, e, op);
    bput(b, " %s ", e->body);
    if (!in_slot && is_checkints(op)) bput(b, "ACC(%d); DROP(); s->fpc = PC + 2;", base_cycles(op));
    else if (in_slot || !is_flush(op)) bput(b, "ACC(%d);", base_cycles(op));
    else bput(b, "ACC(%d); FLUSH(); UPDATE_POINT(PC + 2);", base_cycles(op));
    bput(b, " } /* %s */", e->mn);
}

static uint32_t *blk_gen, bgen;   /* blockset membership for the current function */
static int in_blocks(uint32_t a) { return a >= RAM_BASE && slot_of(a) < nslots && blk_gen[slot_of(a)] == bgen; }

static void go(Buf *b, uint32_t tgt, int check)
{
    if (in_blocks(tgt)) {
        if (!check) bput(b, "goto L_%08X;", tgt);
        else bput(b, "{ if (s->cycles >= s->event_at && SERVICE_AT(hi | 0x%08Xu)) return; goto L_%08X; }", tgt, tgt);
    } else bput(b, "{ s->pc = hi | 0x%08Xu; return; }", tgt);
}

static void slot(Buf *b, uint32_t pc)
{
    uint16_t op = w16(pc);
    int idx = TABLE[op];
    if (idx < 0 || ISA[idx].kind != K_OP) bput(b, "sh3_bad_slot(s, hi | 0x%08Xu);", pc);
    else body_slot(b, op, pc, 1);
}

static void function(FILE *fh, const Func *f, Buf *lines)
{
    bgen++;
    for (uint32_t i = 0; i < f->n; i++) blk_gen[slot_of(f->blocks[i])] = bgen;
    fprintf(fh, "void f_%08X(SH3 *s) {\n    const uint32_t hi = s->pc & 0xE0000000u;\n    switch (s->pc & 0x1FFFFFFFu) {\n", f->entry);
    for (uint32_t i = 0; i < f->n; i++) fprintf(fh, "    case 0x%08Xu: goto L_%08X;\n", f->blocks[i], f->blocks[i]);
    fprintf(fh, "    default: sh3_miss(s); return;\n    }\n");
    for (uint32_t bi = 0; bi < f->n; bi++) {
        uint32_t pc = f->blocks[bi]; int count = 0;
        lines->n = 0;
#define LINE_END() bput(lines, "\n    ")
        for (;;) {
            uint16_t op = w16(pc);
            int idx = TABLE[op];
            if (idx < 0) { bput(lines, "sh3_miss_at(s, hi | 0x%08Xu); return;", pc); LINE_END(); break; }
            const IsaEntry *e = &ISA[idx];
            if (e->kind == K_OP) {
                body_slot(lines, op, pc, 0); LINE_END(); count++; pc += 2;
                if (in_blocks(pc)) { bput(lines, "FLUSH(); UPDATE_POINT(hi | 0x%08Xu); goto L_%08X;", pc, pc); LINE_END(); break; }
                continue;
            }
            bput(lines, "SH3_FETCH(0x%04Xu, (hi | 0x%08Xu)); ACC(%d);", op, pc, base_cycles(op)); LINE_END();
            uint32_t tgt = PHYS(static_target(e->kind, op, pc));
            int r = (op >> 8) & 15;
            if (e->kind == K_CBR) {
                const char *cond = starts(e->mn, "bt") ? "T" : "!T";
                count++;
                bput(lines, "if (%s) { FLUSH(); ", cond); go(lines, tgt, 1); bput(lines, " }"); LINE_END();
                bput(lines, "DROP(); "); go(lines, pc + 2, 0); LINE_END();
            } else if (e->kind == K_CBRS) {
                const char *cond = starts(e->mn, "bt") ? "T" : "!T";
                count += 2;
                bput(lines, "{ const int take = (%s) != 0; if (take) { ", cond); slot(lines, pc + 2); bput(lines, " FLUSH(); "); go(lines, tgt, 1); bput(lines, " } }"); LINE_END();
                uint16_t nxt = w16(pc + 2);
                int plain = TABLE[nxt] >= 0 && ISA[TABLE[nxt]].kind == K_OP;
                bput(lines, "DROP(); ");
                if (plain) { body_slot(lines, nxt, pc + 2, 0); bput(lines, " "); go(lines, pc + 4, 0); }
                else go(lines, pc + 2, 0);
                LINE_END();
            } else {
                count += 2;
                if (e->kind == K_BR) { slot(lines, pc + 2); bput(lines, " FLUSH(); "); go(lines, tgt, 1); }
                else if (e->kind == K_CALL) { bput(lines, "PR = (hi | 0x%08Xu) + 4; ", pc); slot(lines, pc + 2); bput(lines, " FLUSH(); s->pc = hi | 0x%08Xu; return;", tgt); }
                else if (e->kind == K_ICALL || e->kind == K_IJMP) {
                    if (starts(e->mn, "braf") || starts(e->mn, "bsrf")) bput(lines, "{ const uint32_t t = (hi | 0x%08Xu) + 4 + s->r[%d]; ", pc, r);
                    else bput(lines, "{ const uint32_t t = s->r[%d]; ", r);
                    if (e->kind == K_ICALL) bput(lines, "PR = (hi | 0x%08Xu) + 4; ", pc);
                    slot(lines, pc + 2); bput(lines, " FLUSH(); s->pc = t; return; }");
                } else if (e->kind == K_RET) { bput(lines, "{ const uint32_t t = PR; "); slot(lines, pc + 2); bput(lines, " FLUSH(); s->pc = t; return; }"); }
                else if (e->kind == K_RTE) { bput(lines, "{ const uint32_t t = SPC; set_sr(SSR); "); slot(lines, pc + 2); bput(lines, " DROP(); s->pc = t; s->fpc = t; return; }"); }
                else if (e->kind == K_TRAP) { count--; bput(lines, "FLUSH(); s->trap = %d + 1; s->pc = (hi | 0x%08Xu) + 2; return;", op & 0xFF, pc); }
                else if (e->kind == K_SLEEP) { count--; bput(lines, "FLUSH(); s->sleeping = 1; s->pc = (hi | 0x%08Xu) + 2; return;", pc); }
                LINE_END();
            }
            break;
        }
#undef LINE_END
        fprintf(fh, "L_%08X: if (!s->acct) s->cycles += %d;\n    ", f->blocks[bi], count);
        fwrite(lines->p, 1, lines->n - 4, fh);   /* drop the indent after the last line */
    }
    fprintf(fh, "}\n");
}

static const char HEADER[] =
"/* GENERATED from the user's own ROM by core/sh3/gen_c.py. Do not commit, do not distribute. */\n"
"#include \"sh3.h\"\n"
"#define S (s)\n"
"#include \"sh3_ops.h\"\n"
"void sh3_miss(SH3 *s);\n"
"void sh3_miss_at(SH3 *s, uint32_t pc);\n"
"void sh3_bad_slot(SH3 *s, uint32_t pc);\n"
"#define ACC(n) (s->acc += (n))\n"
"#define FLUSH() do { if (s->acct) { s->cycles += s->acc; } s->acc = 0; } while (0)\n"
"#define DROP() (s->acc = 0)\n"
"#define SERVICE_AT(next) (s->pc = (next), s->service ? s->service(s) : 1)\n"
"#define UPDATE_POINT(next) do { if (s->cycles >= s->event_at && SERVICE_AT(next)) return; } while (0)   /* MAME update point: timeslice may end here */\n"
"#define SH3_FETCH(op, pc) do { if (s->fetch_hook) { if ((pc) == s->pcflush) s->fpc = (pc); s->cycles += s->fetch_hook(s->bus.ctx, s->stale_fetch ? s->fpc : (pc), (op)); } } while (0)\n";

int recomp_translate(const uint8_t *ram, uint32_t ram_len, const RecompCfg *c, const char *outdir,
                     int *nfiles, char *err, size_t en)
{
    D = ram; D_len = ram_len; D_end = RAM_BASE + ram_len; nslots = ram_len / 2;
    build_table();
    literals = calloc(nslots, 1); done = calloc(nslots, 1); rejected = calloc(nslots, 1); covered = calloc(nslots, 1);
    seen_gen = calloc(nslots, 4); block_gen = calloc(nslots, 4); blk_gen = calloc(nslots, 4); gen = bgen = 0;
    size_t nf = 0;
    Func *fn = discover(c, &nf, err, en);
    int rc = 0;
    if (!fn) { rc = 0; goto out; }
    int per = c->per_file > 0 ? c->per_file : 150;
    char path[1024]; Buf lines = {0};
    int files = 0;
    for (size_t fi = 0; fi < nf; fi += per, files++) {
        snprintf(path, sizeof path, "%s/recomp_%03d.c", outdir, files);
        FILE *fh = fopen(path, "wb");
        if (!fh) { snprintf(err, en, "can't write %s", path); goto out; }
        fputs(HEADER, fh);
        for (size_t k = fi; k < nf && k < fi + per; k++) function(fh, &fn[k], &lines);
        fclose(fh);
    }
    /* dispatch table: one entry per block address; prefer the function whose entry it is */
    {
        uint32_t *best = calloc(nslots, 4);   /* function entry + 1... stored as entry (0 = none) */
        for (size_t k = 0; k < nf; k++)
            for (uint32_t i = 0; i < fn[k].n; i++) {
                uint32_t b = fn[k].blocks[i], s = slot_of(b);
                if (!best[s] || fn[k].entry == b) best[s] = fn[k].entry;
            }
        snprintf(path, sizeof path, "%s/recomp_table.c", outdir);
        FILE *fh = fopen(path, "wb");
        if (!fh) { snprintf(err, en, "can't write %s", path); free(best); goto out; }
        fputs(HEADER, fh);
        for (size_t k = 0; k < nf; k++) fprintf(fh, "void f_%08X(SH3 *s);\n", fn[k].entry);
        fputs("const struct { uint32_t addr; void (*fn)(SH3 *); } recomp_table[] = {\n", fh);
        size_t ne = 0;
        for (uint32_t s = 0; s < nslots; s++) if (best[s]) { fprintf(fh, "    {0x%08Xu, f_%08X},\n", RAM_BASE + s * 2, best[s]); ne++; }
        fputs("};\nconst unsigned recomp_table_count = sizeof recomp_table / sizeof recomp_table[0];\n", fh);
        fclose(fh); free(best);
        snprintf(err, en, "%zu functions, %zu dispatch entries, %d files", nf, ne, files);
    }
    if (nfiles) *nfiles = files;
    free(lines.p);
    rc = 1;
out:
    for (size_t k = 0; k < nf; k++) free(fn[k].blocks);
    free(fn);
    free(literals); free(done); free(rejected); free(covered); free(seen_gen); free(block_gen); free(blk_gen);
    return rc;
}

#ifdef RECOMP_MAIN
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: recomp <ram.bin> <out_dir> --entry A --vbr A [--vectors A SIZE] [--code-end A] [--extra f] [--per-file N]\n"); return 2; }
    RecompCfg c = {0};
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--entry") && i + 1 < argc) c.main_entry = strtoul(argv[++i], 0, 16);
        else if (!strcmp(argv[i], "--vbr") && i + 1 < argc) c.vbr = strtoul(argv[++i], 0, 16);
        else if (!strcmp(argv[i], "--vectors") && i + 2 < argc) { c.vector_addr = strtoul(argv[++i], 0, 16); c.vector_size = strtoul(argv[++i], 0, 16); }
        else if (!strcmp(argv[i], "--code-end") && i + 1 < argc) c.code_end = strtoul(argv[++i], 0, 16);
        else if (!strcmp(argv[i], "--extra") && i + 1 < argc) c.extra_path = argv[++i];
        else if (!strcmp(argv[i], "--per-file") && i + 1 < argc) c.per_file = atoi(argv[++i]);
    }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *ram = malloc((size_t)n);
    if (fread(ram, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);
    char msg[256];
    int ok_ = recomp_translate(ram, (uint32_t)n, &c, argv[2], NULL, msg, sizeof msg);
    printf("%s\n", msg);
    return ok_ ? 0 : 1;
}
#endif
