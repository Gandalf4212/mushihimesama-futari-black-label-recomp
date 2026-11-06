/* Performance overlay (FPS / CPU % / frame time + graph). GPL-2.0-or-later. */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>
#include <SDL3/SDL.h>

#define HUD_GRAPH 120

typedef struct {
    double freq;
    uint64_t last_present, window_start;
    float graph[HUD_GRAPH], emu_graph[HUD_GRAPH]; int gpos;
    double acc_emu, acc_frame, acc_max, acc_pres; int acc_n, acc_games;
    double fps, game_fps, emu_ms, frame_ms, max_ms, pres_ms;
    const char *backend, *note;   /* shown in the HUD: renderer, mode (e.g. "UNCAPPED") */
} Hud;

void hud_init(Hud *h);
/* Once per present. games: game frames run since the last present (0..3); emu_ticks: performance-counter
 * ticks spent emulating + software-blitting them; pres_ticks: ticks spent drawing + presenting (GPU
 * submission; includes waiting for the GPU or vsync); present_ticks: counter right after presenting. */
void hud_frame(Hud *h, int games, uint64_t emu_ticks, uint64_t pres_ticks, uint64_t present_ticks);
void hud_draw(Hud *h, SDL_Renderer *r, int out_w, int out_h);
/* 5x7 text at pixel size px (upper case; space and unknown characters are blank). */
void hud_text(SDL_Renderer *r, int x, int y, int px, const char *s);

#endif
