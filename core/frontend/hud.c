/* Performance overlay: FPS, emulation time / CPU %, frame time, frame-time graph.
 * Drawn in window space with filled rectangles (always upright, independent of the game rotation).
 * The 5x7 font below is our own (original glyphs: digits, A-Z and a few symbols; also used by the F1 menu).
 * GPU % is not shown yet: the SDL renderer exposes no GPU timing (comes with the SDL3 GPU presenter).
 * GPL-2.0-or-later. */
#include "hud.h"
#include "../runtime/cv1k.h"   /* CV1K_FRAME_HZ */
#include <stdio.h>
#include <string.h>

/* 5x7 glyphs, one byte per row, bit 4 = leftmost column. */
static const struct { char c; unsigned char rows[7]; } font[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}}, {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}}, {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}}, {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}}, {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}}, {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}}, {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}}, {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
};

void hud_text(SDL_Renderer *r, int x, int y, int px, const char *s);
static void text(SDL_Renderer *r, int x, int y, int px, const char *s) { hud_text(r, x, y, px, s); }
static const unsigned char *glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (size_t i = 0; i < sizeof font / sizeof font[0]; i++) if (font[i].c == c) return font[i].rows;
    return NULL;   /* space and unknown characters draw nothing */
}

void hud_text(SDL_Renderer *r, int x, int y, int px, const char *s)
{
    SDL_FRect rc[35 * 64]; int n = 0;
    for (; *s; s++, x += 6 * px) {
        const unsigned char *g = glyph(*s);
        if (!g) continue;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col) && n < (int)(sizeof rc / sizeof rc[0])) {
                    rc[n].x = x + col * px; rc[n].y = y + row * px; rc[n].w = px; rc[n].h = px; n++;
                }
    }
    SDL_RenderFillRects(r, rc, n);
}

void hud_init(Hud *h)
{
    memset(h, 0, sizeof *h);
    h->freq = (double)SDL_GetPerformanceFrequency();
    h->last_present = SDL_GetPerformanceCounter();
    h->window_start = h->last_present;
}

void hud_frame(Hud *h, uint64_t emu_ticks, uint64_t present_ticks)
{
    double emu_ms = (double)emu_ticks * 1000.0 / h->freq;
    double frame_ms = (double)(present_ticks - h->last_present) * 1000.0 / h->freq;
    h->last_present = present_ticks;
    h->graph[h->gpos] = (float)frame_ms; h->emu_graph[h->gpos] = (float)emu_ms;
    h->gpos = (h->gpos + 1) % HUD_GRAPH;
    h->acc_emu += emu_ms; h->acc_frame += frame_ms; if (frame_ms > h->acc_max) h->acc_max = frame_ms; h->acc_n++;
    double win = (double)(present_ticks - h->window_start) / h->freq;
    if (win >= 0.5) {   /* refresh the numbers twice a second */
        h->fps = h->acc_n / win;
        h->emu_ms = h->acc_emu / h->acc_n;
        h->frame_ms = h->acc_frame / h->acc_n;
        h->max_ms = h->acc_max;
        h->acc_emu = h->acc_frame = h->acc_max = 0; h->acc_n = 0;
        h->window_start = present_ticks;
    }
}

void hud_draw(Hud *h, SDL_Renderer *r, int out_w, int out_h)
{
    int px = out_h >= 1400 ? 3 : 2;                      /* font pixel size */
    int lh = 9 * px, x0 = 8, y0 = 8, w = 6 * px * 22 + 2 * px, gh = 24 * px;
    (void)out_w;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 170);
    SDL_FRect bg = {x0 - 4, y0 - 4, w + 8, 4 * lh + gh + 12};
    SDL_RenderFillRect(r, &bg);
    char line[64];
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    snprintf(line, sizeof line, "FPS %6.2f", h->fps);                               text(r, x0, y0, px, line);
    snprintf(line, sizeof line, "CPU %5.1f%%  EMU %5.2f MS", h->emu_ms / (1000.0 / CV1K_FRAME_HZ) * 100.0, h->emu_ms);
    text(r, x0, y0 + lh, px, line);
    snprintf(line, sizeof line, "FRAME %5.2f MS  MAX %5.2f", h->frame_ms, h->max_ms); text(r, x0, y0 + 2 * lh, px, line);
    text(r, x0, y0 + 3 * lh, px, "GPU --");
    /* graph: frame time (white) and emulation time (green) for the last HUD_GRAPH frames, 0-33 ms;
       the line marks one 60 Hz frame (16.7 ms) */
    int gy = y0 + 4 * lh + 4, bw = w / HUD_GRAPH > 0 ? w / HUD_GRAPH : 1;
    double scale = gh / 33.3;
    SDL_FRect bars[HUD_GRAPH], emu[HUD_GRAPH];
    for (int i = 0; i < HUD_GRAPH; i++) {
        int k = (h->gpos + i) % HUD_GRAPH;
        int fh = (int)(h->graph[k] * scale), eh = (int)(h->emu_graph[k] * scale);
        if (fh > gh) fh = gh;
        if (eh > gh) eh = gh;
        bars[i].x = x0 + i * bw; bars[i].w = bw; bars[i].h = fh; bars[i].y = gy + gh - fh;
        emu[i].x = x0 + i * bw; emu[i].w = bw; emu[i].h = eh; emu[i].y = gy + gh - eh;
    }
    SDL_SetRenderDrawColor(r, 200, 200, 200, 200); SDL_RenderFillRects(r, bars, HUD_GRAPH);
    SDL_SetRenderDrawColor(r, 60, 220, 90, 230);   SDL_RenderFillRects(r, emu, HUD_GRAPH);
    SDL_SetRenderDrawColor(r, 255, 80, 80, 255);
    int ly = gy + gh - (int)(16.683 * scale);
    SDL_RenderLine(r, x0, ly, x0 + HUD_GRAPH * bw, ly);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}
