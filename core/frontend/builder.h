#ifndef BUILDER_H
#define BUILDER_H
#include <stddef.h>
/* 1 if the bundled toolchain (toolchain/) and runtime sources (recomp/) are next to the exe. */
int game_build_available(void);
/* Builds assets/game.dll from assets/program.bin with a progress screen. 1 = done; 0 = failed (why). */
int game_build(char *why, size_t wn);
#endif
