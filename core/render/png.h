#ifndef PNG_H
#define PNG_H
#include <stdint.h>
int png_write_rgb(const char *path, const uint32_t *px, int w, int h);   /* px = 0x00RRGGBB */
#endif
