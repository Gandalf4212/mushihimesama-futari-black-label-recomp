#ifndef DISPATCH_H
#define DISPATCH_H
#include "../sh3/sh3.h"
typedef void (*recomp_fn)(SH3 *);
void dispatch_init(uint32_t ram_size);
void dispatch_run(SH3 *s, uint64_t until_cycles, uint32_t stop_pc);   /* stop_pc 0 = none */
void dispatch_report(void);
void dispatch_step(SH3 *s);
void dispatch_write_misses(const char *path);   /* feed to discover.py --extra */
#endif
