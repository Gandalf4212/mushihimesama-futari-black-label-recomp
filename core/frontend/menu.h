/* F1 settings menu: an overlay drawn in window space; the game is paused while it is open.
 * GPL-2.0-or-later. */
#ifndef MENU_H
#define MENU_H
#include <SDL3/SDL.h>
#include "config.h"

typedef struct {
    int open, sel;
    int dirty;              /* a setting changed since the menu opened (save on close) */
    Uint64 repeat_at;       /* gamepad axis auto-repeat */
} Menu;

/* What menu_event changed, for the caller to apply. */
enum {
    MENU_CH_ORIENT = 1, MENU_CH_SCALE = 2, MENU_CH_FULLSCREEN = 4, MENU_CH_VSYNC = 8,
    MENU_CH_SLOWDOWN = 16, MENU_CH_RATE = 32, MENU_CH_CLOSED = 64, MENU_CH_QUIT = 128
};

void menu_init(Menu *m);
/* Handles one event (only when open, except F1 / the gamepad Guide button which open it).
 * show_hud is the live HUD flag. Returns MENU_CH_* bits. */
int  menu_event(Menu *m, Config *c, int *show_hud, const SDL_Event *e);
void menu_draw(const Menu *m, const Config *c, int show_hud, SDL_Renderer *r, int out_w, int out_h);

#endif
