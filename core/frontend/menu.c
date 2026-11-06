/* F1 settings menu. Keyboard: up/down select, left/right (or Enter) change, F1 / Esc close.
 * Gamepad: Guide (or Back+Start) opens it, d-pad / left stick navigate, A change, B close.
 * Changes apply at once (the caller applies the returned MENU_CH_* bits) and are saved to
 * config.toml when the menu closes. GPL-2.0-or-later. */
#include "menu.h"
#include "hud.h"
#include <stdio.h>
#include <string.h>

enum { I_ORIENT, I_SCALE, I_FULLSCREEN, I_VSYNC, I_RATE, I_SLOWDOWN, I_FREEPLAY, I_VOLUME, I_ROTCTL, I_HUD,
       I_RESUME, I_QUIT, I_COUNT };

void menu_init(Menu *m) { memset(m, 0, sizeof *m); }

static void value(const Config *c, int show_hud, int i, char *out, size_t n)
{
    static const char *orient[] = {"YOKO", "TATE CW", "TATE CCW"};
    static const char *rate[] = {"NATIVE 60.024 HZ", "DISPLAY REFRESH"};
    switch (i) {
    case I_ORIENT: snprintf(out, n, "%s", orient[c->orientation]); break;
    case I_SCALE: if (c->scale) snprintf(out, n, "%dX", c->scale); else snprintf(out, n, "AUTO"); break;
    case I_FULLSCREEN: snprintf(out, n, "%s", c->fullscreen ? "ON" : "OFF"); break;
    case I_VSYNC: snprintf(out, n, "%s", c->vsync ? "ON" : "OFF"); break;
    case I_RATE: snprintf(out, n, "%s", rate[c->rate]); break;
    case I_SLOWDOWN: snprintf(out, n, "%s", c->slowdown ? "ON (ORIGINAL)" : "OFF"); break;
    case I_FREEPLAY: snprintf(out, n, "%s", c->freeplay ? "ON" : "OFF (COINS)"); break;
    case I_VOLUME: snprintf(out, n, "%d", c->volume); break;
    case I_ROTCTL: snprintf(out, n, "%s", c->rotate_controls ? "ON" : "OFF"); break;
    case I_HUD: snprintf(out, n, "%s", show_hud ? "ON" : "OFF"); break;
    default: out[0] = 0;
    }
}

static const char *label[I_COUNT] = {
    "ORIENTATION", "WINDOW SCALE", "FULLSCREEN", "VSYNC", "GAME SPEED", "SLOWDOWN", "FREE PLAY", "VOLUME",
    "ROTATE CONTROLS", "PERFORMANCE HUD", "RESUME", "QUIT"
};

/* dir = -1 / +1 (left / right), 0 = activate (Enter / A) */
static int change(Menu *m, Config *c, int *show_hud, int dir)
{
    int d = dir ? dir : 1, ch = 0;
    switch (m->sel) {
    case I_ORIENT: c->orientation = (Orientation)((c->orientation + 3 + d) % 3); ch = MENU_CH_ORIENT; break;
    case I_SCALE: c->scale = (c->scale + 9 + d) % 9; ch = MENU_CH_SCALE; break;       /* 0 = auto, 1..8 */
    case I_FULLSCREEN: c->fullscreen = !c->fullscreen; ch = MENU_CH_FULLSCREEN; break;
    case I_VSYNC: c->vsync = !c->vsync; ch = MENU_CH_VSYNC; break;
    case I_RATE: c->rate = (c->rate + 2 + d) % 2; ch = MENU_CH_RATE; break;
    case I_SLOWDOWN: c->slowdown = !c->slowdown; ch = MENU_CH_SLOWDOWN; break;
    case I_FREEPLAY: c->freeplay = !c->freeplay; break;
    case I_VOLUME: c->volume += 10 * d; if (c->volume < 0) c->volume = 0; if (c->volume > 100) c->volume = 100; break;
    case I_ROTCTL: c->rotate_controls = !c->rotate_controls; break;
    case I_HUD: *show_hud = !*show_hud; c->hud = *show_hud; break;
    case I_RESUME: if (!dir) { m->open = 0; return MENU_CH_CLOSED; } return 0;
    case I_QUIT: if (!dir) return MENU_CH_QUIT; return 0;
    }
    m->dirty = 1;
    return ch;
}

static int move(Menu *m, int d) { m->sel = (m->sel + I_COUNT + d) % I_COUNT; return 0; }

int menu_event(Menu *m, Config *c, int *show_hud, const SDL_Event *e)
{
    if (!m->open) {
        if ((e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_F1 && !e->key.repeat) ||
            (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)) {
            m->open = 1; m->dirty = 0;
        }
        if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            (e->gbutton.button == SDL_GAMEPAD_BUTTON_START || e->gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
            SDL_Gamepad *g = SDL_GetGamepadFromID(e->gbutton.which);   /* Back + Start (XInput often hides Guide) */
            if (g && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START) && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) { m->open = 1; m->dirty = 0; }
        }
        return 0;
    }
    if (e->type == SDL_EVENT_KEY_DOWN) {
        switch (e->key.key) {
        case SDLK_F1: case SDLK_ESCAPE: if (e->key.repeat) return 0; m->open = 0; return MENU_CH_CLOSED;
        case SDLK_UP: return move(m, -1);
        case SDLK_DOWN: return move(m, 1);
        case SDLK_LEFT: return change(m, c, show_hud, -1);
        case SDLK_RIGHT: return change(m, c, show_hud, 1);
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: case SDLK_Z: return change(m, c, show_hud, 0);
        }
    }
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (e->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_GUIDE: case SDL_GAMEPAD_BUTTON_EAST: m->open = 0; return MENU_CH_CLOSED;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return move(m, -1);
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return move(m, 1);
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return change(m, c, show_hud, -1);
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return change(m, c, show_hud, 1);
        case SDL_GAMEPAD_BUTTON_SOUTH: return change(m, c, show_hud, 0);
        }
    }
    if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY || e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)) {
        int v = e->gaxis.value, dir = v > 20000 ? 1 : v < -20000 ? -1 : 0;
        if (!dir) { m->repeat_at = 0; return 0; }
        Uint64 now = SDL_GetTicks();
        if (m->repeat_at && now < m->repeat_at) return 0;
        m->repeat_at = now + (m->repeat_at ? 120 : 350);
        if (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) return move(m, dir);
        return change(m, c, show_hud, dir);
    }
    return 0;
}

void menu_draw(const Menu *m, const Config *c, int show_hud, SDL_Renderer *r, int out_w, int out_h)
{
    int px = out_h >= 900 ? 3 : 2, lh = 11 * px, cw = 6 * px;
    int w = 40 * cw, h = (I_COUNT + 4) * lh;
    int x0 = (out_w - w) / 2, y0 = (out_h - h) / 2;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 210);
    SDL_FRect bg = {x0, y0, w, h}; SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    hud_text(r, x0 + cw * 2, y0 + lh / 2, px, "SETTINGS");
    SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
    hud_text(r, x0 + cw * 2, y0 + h - lh, px, "<> CHANGE   F1 / ESC / B CLOSE");
    for (int i = 0; i < I_COUNT; i++) {
        int y = y0 + (i + 2) * lh;
        if (i == m->sel) {
            SDL_SetRenderDrawColor(r, 60, 90, 160, 255);
            SDL_FRect hl = {x0 + cw, y - px * 2, w - 2 * cw, lh}; SDL_RenderFillRect(r, &hl);
        }
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        hud_text(r, x0 + cw * 2, y, px, label[i]);
        char v[32]; value(c, show_hud, i, v, sizeof v);
        SDL_SetRenderDrawColor(r, 255, 220, 120, 255);
        hud_text(r, x0 + cw * 21, y, px, v);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}
