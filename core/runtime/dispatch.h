#ifndef DISPATCH_H
#define DISPATCH_H
#include "../sh3/sh3.h"
#include "recomp_abi.h"
typedef void (*recomp_fn)(SH3 *);
void dispatch_init(uint32_t ram_size, const RecompEntry *table, unsigned count);   /* table NULL = interpreter only */
const RecompHost *dispatch_host(void);
/* Statically linked generated code (gen_c.py recomp_table.c): Linux tools and builds. */
extern const RecompEntry recomp_table[];
extern const unsigned recomp_table_count;
void dispatch_run(SH3 *s, uint64_t until_cycles, uint32_t stop_pc);   /* stop_pc 0 = none */
void dispatch_report(void);
void dispatch_step(SH3 *s);
void dispatch_write_misses(const char *path);   /* feed to discover.py --extra */
#endif
