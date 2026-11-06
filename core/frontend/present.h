/* Presentation: places the 320x240 game picture in the output (integer or fit scaling, rotation),
 * draws it through our shaders on SDL3's GPU renderer (Vulkan), and draws the Yoko bezel.
 * Falls back to plain nearest-neighbour drawing when the GPU renderer or SPIR-V is unavailable.
 * GPL-2.0-or-later. */
#ifndef PRESENT_H
#define PRESENT_H
#include <SDL3/SDL.h>
#include "config.h"
#include "layout.h"

typedef struct {
    SDL_Renderer *ren;
    SDL_Texture *tex;                 /* 320x240 game picture */
    SDL_GPURenderState *game, *bezel; /* NULL when shaders are unavailable */
    SDL_Texture *bezel_img;           /* artwork = a single PNG overlay */
    Layout lay; int have_lay;         /* artwork = a MAME .lay bezel pack */
    int gpu;                          /* 1 = GPU renderer with our shaders */
} Presenter;

/* Creates the renderer for win (GPU renderer on Vulkan when possible) and the game texture. */
int  present_init(Presenter *p, SDL_Window *win, Config *c);
/* Loads config artwork (sets c->artwork_views). */
void present_load_bezel(Presenter *p, Config *c);
/* Uploads fb (XRGB8888 320x240) if non-NULL and draws picture + bezel for output size ow x oh. */
void present_draw(Presenter *p, const Config *c, Orientation o, const uint32_t *fb, int ow, int oh);
const char *present_backend(const Presenter *p);
#endif
