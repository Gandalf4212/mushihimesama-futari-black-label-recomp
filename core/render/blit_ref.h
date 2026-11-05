#ifndef BLIT_REF_H
#define BLIT_REF_H
#include <stdint.h>
#define BLIT_VRAM_W 8192
#define BLIT_VRAM_H 4096
typedef struct {
    uint16_t *vram;            /* ARGB1555: bit15 = opaque ("transparency" bit), 5:5:5 RGB */
    uint64_t draws, uploads, bad_ops;
    uint8_t mode_use[128]; uint8_t mode_tag;   /* debug: which (blend, smode, dmode) were used, tagged by phase */
} BlitRef;
void blit_ref_init(BlitRef *b);
/* Execute one command list from main RAM (big-endian bytes, mask = ram_size - 1). */
void blit_ref_exec(BlitRef *b, const uint8_t *ram, uint32_t ram_mask, uint32_t list, uint32_t clip_x, uint32_t clip_y);
/* Visible 320x240 frame at the given scroll, as 0x00RRGGBB with 5-bit channels << 3 (as MAME). */
void blit_ref_frame(const BlitRef *b, uint32_t scroll_x, uint32_t scroll_y, uint32_t *rgb_out);
#endif
