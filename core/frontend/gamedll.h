#ifndef GAMEDLL_H
#define GAMEDLL_H
#include <stddef.h>
#include "../runtime/dispatch.h"
#include "game_config.h"
/* Finds the compiled game code. Returns 1 with the table set, or 0 (table NULL: run on the interpreter)
 * with a reason in why. */
int game_code_load(const RecompEntry **table, unsigned *count, char *why, size_t wn);
#endif
