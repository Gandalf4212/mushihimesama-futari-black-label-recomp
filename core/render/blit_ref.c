/* CV1000 blitter: CPU reference implementation (ground truth for the Vulkan renderer).
 * Behavior follows MAME's cv1k_v.cpp / cv1k_v_in.ipp / cv1k_v_pixel.ipp (BSD-3-Clause, David Haywood
 * and contributors), re-implemented as one readable routine. Exactness vs MAME is the goal, including
 * MAME's add_with_clr_square quirk (green/blue use clr0.r), so frames can be compared bit for bit.
 * GPL-2.0-or-later. */
#include "blit_ref.h"
#include <stdlib.h>
#include <string.h>

#define CLIP_MARGIN 32

static uint8_t mul_t[0x20][0x40];   /* colrtable[x][y] = min(x*y/31, 31) */
static uint8_t mulr_t[0x20][0x40];  /* colrtable_rev[x^31][y] = min(x*y/31, 31) */
static uint8_t add_t[0x20][0x20];   /* min(x+y, 31) */

void blit_ref_init(BlitRef *b)
{
    for (int y = 0; y < 0x40; y++)
        for (int x = 0; x < 0x20; x++) {
            int v = (x * y) / 0x1f; if (v > 0x1f) v = 0x1f;
            mul_t[x][y] = v; mulr_t[x ^ 0x1f][y] = v;
        }
    for (int y = 0; y < 0x20; y++)
        for (int x = 0; x < 0x20; x++) add_t[x][y] = (x + y) > 0x1f ? 0x1f : x + y;
    if (!b->vram) b->vram = calloc(BLIT_VRAM_W * BLIT_VRAM_H, sizeof(uint16_t));
}

typedef struct { uint8_t r, g, b; } C;
static inline C unpack(uint16_t p) { return (C){(p >> 10) & 31, (p >> 5) & 31, p & 31}; }
static inline uint16_t pack(C c, uint16_t t) { return (uint16_t)(t | c.r << 10 | c.g << 5 | c.b); }

/* clr_t helpers, same argument order as MAME */
#define MUL(a, b) mul_t[(a)][(b)]
#define MULR(a, b) mulr_t[(a)][(b)]
#define ADD(a, b) add_t[(a)][(b)]

static inline C blend(int smode, int dmode, C s, C d, int sa, int da)
{
    C r, c0;
    if (smode == 0) {
        switch (dmode) {
        case 0: return (C){ADD(MUL(sa, s.r), MUL(da, d.r)), ADD(MUL(sa, s.g), MUL(da, d.g)), ADD(MUL(sa, s.b), MUL(da, d.b))};
        case 1: return (C){ADD(MUL(sa, s.r), MUL(s.r, d.r)), ADD(MUL(sa, s.g), MUL(s.g, d.g)), ADD(MUL(sa, s.b), MUL(s.b, d.b))};
        case 5: return (C){ADD(MUL(sa, s.r), MULR(s.r, d.r)), ADD(MUL(sa, s.g), MULR(s.g, d.g)), ADD(MUL(sa, s.b), MULR(s.b, d.b))};
        }
        c0 = (C){MUL(sa, s.r), MUL(sa, s.g), MUL(sa, s.b)};          /* mul_fixed(s_alpha, s) */
    } else if (smode == 2) {
        if (dmode == 0)
            return (C){ADD(MUL(d.r, s.r), MUL(da, d.r)), ADD(MUL(d.g, s.g), MUL(da, d.g)), ADD(MUL(d.b, s.b), MUL(da, d.b))};
        c0 = (C){MUL(d.r, s.r), MUL(d.g, s.g), MUL(d.b, s.b)};       /* mul_3param(s, d) */
    } else {
        switch (smode) {
        case 1: c0 = (C){MUL(s.r, s.r), MUL(s.g, s.g), MUL(s.b, s.b)}; break;          /* square */
        case 4: c0 = (C){MULR(sa, s.r), MULR(sa, s.g), MULR(sa, s.b)}; break;          /* mul_fixed_rev */
        case 5: c0 = (C){MULR(s.r, s.r), MULR(s.g, s.g), MULR(s.b, s.b)}; break;       /* mul_rev_square */
        case 6: c0 = (C){MULR(d.r, s.r), MULR(d.g, s.g), MULR(d.b, s.b)}; break;       /* mul_rev_3param */
        default: c0 = s; break;                                                        /* 3, 7: copy */
        }
    }
    switch (dmode) {
    case 0: r = (C){ADD(c0.r, MUL(d.r, da)), ADD(c0.g, MUL(d.g, da)), ADD(c0.b, MUL(d.b, da))}; break;   /* add_with_clr_mul_fixed */
    case 1: r = (C){ADD(c0.r, MUL(s.r, d.r)), ADD(c0.g, MUL(s.g, d.g)), ADD(c0.b, MUL(s.b, d.b))}; break; /* mul_3param(c0, d, s) */
    case 2: r = (C){ADD(c0.r, MUL(d.r, d.r)), ADD(c0.r, MUL(d.g, d.g)), ADD(c0.r, MUL(d.b, d.b))}; break; /* square (MAME quirk) */
    case 4: r = (C){ADD(c0.r, MULR(da, d.r)), ADD(c0.g, MULR(da, d.g)), ADD(c0.b, MULR(da, d.b))}; break; /* mul_fixed_rev */
    case 5: r = (C){ADD(c0.r, MULR(s.r, d.r)), ADD(c0.g, MULR(s.g, d.g)), ADD(c0.b, MULR(s.b, d.b))}; break;
    case 6: r = (C){ADD(c0.r, MULR(d.r, d.r)), ADD(c0.g, MULR(d.g, d.g)), ADD(c0.b, MULR(d.b, d.b))}; break;
    default: r = (C){ADD(c0.r, d.r), ADD(c0.g, d.g), ADD(c0.b, d.b)}; break;                              /* 3, 7: add */
    }
    return r;
}

typedef struct { int min_x, max_x, min_y, max_y; } Clip;

static void draw(BlitRef *b, const Clip *clip, uint32_t attr, uint32_t alpha, int src_x, int src_y,
                 int dst_x, int dst_y, int dimx, int dimy, int tr, int tg, int tb)
{
    Clip cl = *clip;                                   /* never touch outside VRAM */
    if (cl.min_x < 0) cl.min_x = 0;
    if (cl.min_y < 0) cl.min_y = 0;
    if (cl.max_x > BLIT_VRAM_W - 1) cl.max_x = BLIT_VRAM_W - 1;
    if (cl.max_y > BLIT_VRAM_H - 1) cl.max_y = BLIT_VRAM_H - 1;
    clip = &cl;
    int dmode = attr & 7, smode = (attr >> 4) & 7;
    int trans = (attr >> 8) & 1, bl = (attr >> 9) & 1, flipy = (attr >> 10) & 1, flipx = (attr >> 11) & 1;
    int da = (alpha & 0xFF) >> 3, sa = ((alpha >> 8) & 0xFF) >> 3;
    C tint = {tr >> 2, tg >> 2, tb >> 2};
    int tinted = tint.r != 0x20 || tint.g != 0x20 || tint.b != 0x20;
    if (smode == 0 && sa == 0x1F && dmode == 4 && da == 0x1F) bl = 0;

    if (flipx) src_x += dimx - 1;
    int yf = 1;
    if (flipy) { yf = -1; src_y += dimy - 1; }
    int starty = 0;
    if (dst_y < clip->min_y) starty = clip->min_y - dst_y;
    if (dst_y + dimy > clip->max_y) dimy -= (dst_y + dimy - 1) - clip->max_y;
    if (flipx) { if ((src_x & 0x1FFF) < ((src_x - (dimx - 1)) & 0x1FFF)) return; }
    else       { if ((src_x & 0x1FFF) > ((src_x + (dimx - 1)) & 0x1FFF)) return; }
    int startx = 0;
    if (dst_x < clip->min_x) startx = clip->min_x - dst_x;
    if (dst_x + dimx > clip->max_x) dimx -= (dst_x + dimx - 1) - clip->max_x;

    for (int y = starty; y < dimy; y++) {
        uint16_t *dst = &b->vram[(dst_y + y) * BLIT_VRAM_W + dst_x + startx];
        const uint16_t *src = &b->vram[((src_y + yf * y) & 0x0FFF) * BLIT_VRAM_W];
        int sx = flipx ? src_x - startx : src_x + startx, step = flipx ? -1 : 1;
        for (int x = startx; x < dimx; x++, dst++, sx += step) {
            uint16_t pen = src[sx];
            if (!bl && !tinted) {                        /* "really simple" */
                if (!trans || (pen & 0x8000)) *dst = pen;
                continue;
            }
            if (trans && !(pen & 0x8000)) continue;
            C s = unpack(pen);
            if (tinted) s = (C){MUL(s.r, tint.r), MUL(s.g, tint.g), MUL(s.b, tint.b)};
            if (bl) s = blend(smode, dmode, s, unpack(*dst), sa, da);
            *dst = pack(s, pen & 0x8000);
        }
    }
}

static inline uint16_t rd16(const uint8_t *ram, uint32_t mask, uint32_t *a)
{
    uint32_t o = *a & mask; *a += 2;
    return (uint16_t)(ram[o] << 8 | ram[o + 1]);
}

void blit_ref_exec(BlitRef *b, const uint8_t *ram, uint32_t ram_mask, uint32_t list, uint32_t clip_x, uint32_t clip_y)
{
    uint32_t a = list & 0x1FFFFFFFu;
    Clip screen = {(int)clip_x - CLIP_MARGIN, (int)clip_x + 319 + CLIP_MARGIN, (int)clip_y - CLIP_MARGIN, (int)clip_y + 239 + CLIP_MARGIN};
    Clip clip = screen;
    for (int guard = 0; guard < 1 << 20; guard++) {
        uint16_t op = rd16(ram, ram_mask, &a);
        switch (op & 0xF000) {
        case 0x0000: case 0xF000: return;
        case 0xC000:
            clip = rd16(ram, ram_mask, &a) ? screen : (Clip){0, BLIT_VRAM_W - 1, 0, BLIT_VRAM_H - 1};
            break;
        case 0x2000: {                                   /* upload ARGB1555 from RAM to VRAM */
            a += 6;
            uint32_t dx = rd16(ram, ram_mask, &a) & 0x1FFF, dy = rd16(ram, ram_mask, &a) & 0x0FFF;
            uint32_t w = (rd16(ram, ram_mask, &a) & 0x1FFF) + 1, h = (rd16(ram, ram_mask, &a) & 0x0FFF) + 1;
            for (uint32_t y = 0; y < h; y++)
                for (uint32_t x = 0; x < w; x++) {
                    uint16_t p = rd16(ram, ram_mask, &a);
                    uint32_t yy = dy + y, xx = dx + x;
                    if (yy < BLIT_VRAM_H && xx < BLIT_VRAM_W) b->vram[yy * BLIT_VRAM_W + xx] = p;
                }
            b->uploads++;
            break;
        }
        case 0x1000: {                                   /* draw */
            a -= 2;
            uint16_t attr = rd16(ram, ram_mask, &a), alpha = rd16(ram, ram_mask, &a);
            int sx = rd16(ram, ram_mask, &a) & 0x1FFF, sy = rd16(ram, ram_mask, &a) & 0x0FFF;
            int dx = (int16_t)rd16(ram, ram_mask, &a), dy = (int16_t)rd16(ram, ram_mask, &a);
            int w = (rd16(ram, ram_mask, &a) & 0x1FFF) + 1, h = (rd16(ram, ram_mask, &a) & 0x0FFF) + 1;
            uint16_t tr = rd16(ram, ram_mask, &a), tgb = rd16(ram, ram_mask, &a);
            draw(b, &clip, attr, alpha, sx, sy, dx, dy, w, h, tr & 0xFF, tgb >> 8, tgb & 0xFF);
            b->draws++;
            break;
        }
        default: b->bad_ops++; return;
        }
    }
}

void blit_ref_frame(const BlitRef *b, uint32_t scroll_x, uint32_t scroll_y, uint32_t *rgb_out /* 320x240 */)
{
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            uint16_t p = b->vram[((y + scroll_y) & 0x0FFF) * BLIT_VRAM_W + ((x + scroll_x) & 0x1FFF)];
            rgb_out[y * 320 + x] = (uint32_t)((p >> 10) & 31) << 19 | (uint32_t)((p >> 5) & 31) << 11 | (uint32_t)(p & 31) << 3;
        }
}
