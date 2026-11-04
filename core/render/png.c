/* Minimal PNG writer (stored deflate blocks, no compression library needed). GPL-2.0-or-later. */
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t crc_table[256];
static uint32_t crc(const uint8_t *p, size_t n, uint32_t c)
{
    if (!crc_table[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t v = i; for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crc_table[i] = v; }
    c = ~c; while (n--) c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8); return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t h[8]; be32(h, n); memcpy(h + 4, type, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t c = crc((const uint8_t *)type, 4, 0); c = crc(data, n, c);
    uint8_t t[4]; be32(t, c); fwrite(t, 1, 4, f);
}
int png_write_rgb(const char *path, const uint32_t *px, int w, int h)
{
    size_t raw_n = (size_t)h * (1 + 3 * w);
    uint8_t *raw = malloc(raw_n), *q = raw;
    for (int y = 0; y < h; y++) { *q++ = 0; for (int x = 0; x < w; x++) { uint32_t v = px[y * w + x]; *q++ = v >> 16; *q++ = v >> 8; *q++ = v; } }
    size_t nblk = (raw_n + 65534) / 65535, z_n = 2 + raw_n + nblk * 5 + 4;
    uint8_t *z = malloc(z_n), *o = z; *o++ = 0x78; *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        uint32_t len = raw_n - off > 65535 ? 65535 : (uint32_t)(raw_n - off);
        *o++ = off + len >= raw_n; *o++ = len; *o++ = len >> 8; *o++ = ~len; *o++ = ~len >> 8;
        memcpy(o, raw + off, len); o += len;
    }
    be32(o, b << 16 | a); o += 4;
    FILE *f = fopen(path, "wb"); if (!f) { free(raw); free(z); return 0; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ih[13]; be32(ih, w); be32(ih + 4, h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, (uint32_t)(o - z)); chunk(f, "IEND", NULL, 0);
    fclose(f); free(raw); free(z); return 1;
}
