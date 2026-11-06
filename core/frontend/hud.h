/* Performance overlay (FPS / CPU % / frame time + graph). GPL-2.0-or-later. */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>
#include <SDL.h>

#define HUD_GRAPH 120

typedef struct {
    double freq;
    uint64_t last_present, window_start;
    float graph[HUD_GRAPH], emu_graph[HUD_GRAPH]; int gpos;
    double acc_emu, acc_frame, acc_max; int acc_n;
    double fps, emu_ms, frame_ms, max_ms;
} Hud;

void hud_init(Hud *h);
/* emu_ticks: performance-counter ticks spent emulating + rendering this frame;
 * present_ticks: performance counter right after the frame was presented. */
void hud_frame(Hud *h, uint64_t emu_ticks, uint64_t present_ticks);
void hud_draw(Hud *h, SDL_Renderer *r, int out_w, int out_h);
/* 5x7 text at pixel size px (upper case; space and unknown characters are blank). */
void hud_text(SDL_Renderer *r, int x, int y, int px, const char *s);

#endif
