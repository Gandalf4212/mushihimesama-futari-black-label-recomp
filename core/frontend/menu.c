/* F1 settings menu. Keyboard: up/down select, left/right (or Enter) change, F1 / Esc close.
 * Gamepad: Guide (or Back+Start) opens it, d-pad / left stick navigate, A change, B close.
 * Changes apply at once (the caller applies the returned MENU_CH_* bits) and are saved to
 * config.toml when the menu closes. GPL-2.0-or-later. */
#include "menu.h"
#include "version.h"
#include "hud.h"
#include <stdio.h>
#include <string.h>

enum { I_ORIENT, I_SCALE, I_SCALING, I_SHADER, I_BEZEL, I_FULLSCREEN, I_VSYNC, I_UNCAPPED, I_RATE, I_SLOWDOWN, I_FREEPLAY, I_VOLUME, I_ROTCTL, I_RUMBLE1, I_RUMBLE2, I_HUD,
       I_CONTROLS, I_RESUME, I_QUIT, I_COUNT };

void menu_init(Menu *m) { memset(m, 0, sizeof *m); }

static void value(const Config *c, int show_hud, int i, char *out, size_t n)
{
    static const char *orient[] = {"YOKO", "TATE CW", "TATE CCW"};
    static const char *rate[] = {"NATIVE 60.024 HZ", "DISPLAY REFRESH"};
    switch (i) {
    case I_ORIENT: snprintf(out, n, "%s", orient[c->orientation]); break;
    case I_SCALE: if (c->scale) snprintf(out, n, "%dX", c->scale); else snprintf(out, n, "AUTO"); break;
    case I_SCALING: snprintf(out, n, "%s", c->scaling == SCALING_INTEGER ? "INTEGER" : "FIT (KEEP ASPECT)"); break;
    case I_SHADER: { static const char *sh[] = {"NONE", "SHARP", "CRT", "CRT CURVED"}; snprintf(out, n, "%s", sh[c->shader]); break; }
    case I_BEZEL: if (c->bezel >= 2) snprintf(out, n, "ARTWORK VIEW %d", c->bezel - 1); else snprintf(out, n, "%s", c->bezel ? "OURS" : "OFF"); break;
    case I_UNCAPPED: snprintf(out, n, "%s", c->uncapped ? "ON (BENCHMARK)" : "OFF"); break;
    case I_FULLSCREEN: snprintf(out, n, "%s", c->fullscreen ? "ON" : "OFF"); break;
    case I_VSYNC: snprintf(out, n, "%s", c->vsync ? "ON" : "OFF"); break;
    case I_RATE: snprintf(out, n, "%s", rate[c->rate]); break;
    case I_SLOWDOWN: snprintf(out, n, "%s", c->slowdown ? "ON (ORIGINAL)" : "OFF"); break;
    case I_FREEPLAY: snprintf(out, n, "%s", c->freeplay ? "ON" : "OFF (COINS)"); break;
    case I_VOLUME: snprintf(out, n, "%d", c->volume); break;
    case I_ROTCTL: snprintf(out, n, "%s", c->rotate_controls ? "ON" : "OFF"); break;
    case I_RUMBLE1: case I_RUMBLE2: { static const char *rm[] = {"OFF", "LOW", "HIGH"}; snprintf(out, n, "%s", rm[c->rumble[i - I_RUMBLE1]]); break; }
    case I_HUD: snprintf(out, n, "%s", show_hud ? "ON" : "OFF"); break;
    default: out[0] = 0;
    }
}

static const char *label[I_COUNT] = {
    "ORIENTATION", "WINDOW SCALE", "SCALING", "SHADER", "BEZEL", "FULLSCREEN", "VSYNC", "UNCAPPED FPS", "GAME SPEED", "SLOWDOWN", "FREE PLAY", "VOLUME",
    "ROTATE CONTROLS", "RUMBLE P1", "RUMBLE P2", "PERFORMANCE HUD", "CONTROLS...", "RESUME", "QUIT"
};

/* dir = -1 / +1 (left / right), 0 = activate (Enter / A) */
static int change(Menu *m, Config *c, int *show_hud, int dir)
{
    int d = dir ? dir : 1, ch = 0;
    switch (m->sel) {
    case I_ORIENT: c->orientation = (Orientation)((c->orientation + 3 + d) % 3); ch = MENU_CH_ORIENT; break;
    case I_SCALE: c->scale = (c->scale + 9 + d) % 9; ch = MENU_CH_SCALE; break;       /* 0 = auto, 1..8 */
    case I_SCALING: c->scaling = !c->scaling; break;
    case I_SHADER: c->shader = (c->shader + 4 + d) % 4; break;
    case I_BEZEL: { int nb = 2 + c->artwork_views; c->bezel = (c->bezel + nb + d) % nb; break; }
    case I_UNCAPPED: c->uncapped = !c->uncapped; ch = MENU_CH_VSYNC; break;
    case I_FULLSCREEN: c->fullscreen = !c->fullscreen; ch = MENU_CH_FULLSCREEN; break;
    case I_VSYNC: c->vsync = !c->vsync; ch = MENU_CH_VSYNC; break;
    case I_RATE: c->rate = (c->rate + 2 + d) % 2; ch = MENU_CH_RATE; break;
    case I_SLOWDOWN: c->slowdown = !c->slowdown; ch = MENU_CH_SLOWDOWN; break;
    case I_FREEPLAY: c->freeplay = !c->freeplay; break;
    case I_VOLUME: c->volume += 10 * d; if (c->volume < 0) c->volume = 0; if (c->volume > 100) c->volume = 100; break;
    case I_ROTCTL: c->rotate_controls = !c->rotate_controls; break;
    case I_RUMBLE1: case I_RUMBLE2: { int *r = &c->rumble[m->sel - I_RUMBLE1]; *r = (*r + 3 + d) % 3; break; }
    case I_HUD: *show_hud = !*show_hud; c->hud = *show_hud; break;
    case I_CONTROLS: if (!dir) { m->page = 1; m->csel = 0; m->capture = 0; } return 0;
    case I_RESUME: if (!dir) { m->open = 0; return MENU_CH_CLOSED; } return 0;
    case I_QUIT: if (!dir) return MENU_CH_QUIT; return 0;
    }
    m->dirty = 1;
    return ch;
}

static int move(Menu *m, int d) { m->sel = (m->sel + I_COUNT + d) % I_COUNT; return 0; }

/* ---------------- CONTROLS page ----------------
 * One row per action. While a row waits for input, a key binds a key (up to two per action) and a pad
 * button binds the pad (button rows only; both pads share the pad mapping, directions are always
 * d-pad + left stick). Delete clears a row's keys, X (pad) clears its pad button. */
enum { C_RESET = 0, C_FIRST = 1, C_BACK = ACT_COUNT + 1, C_ROWS };   /* row 0 = reset, then the actions */
#define ROW_ACT(r) ((r) - C_FIRST)   /* row -> action (valid for C_FIRST <= r < C_BACK) */

static int pad_slot(int r) { int a = ROW_ACT(r); return r >= C_FIRST && a < 2 * ACT_PER_PLAYER ? a % ACT_PER_PLAYER : -1; }   /* -1: keys only */

static void crow_label(int row, char *out, size_t n)
{
    if (row == C_RESET || row == C_BACK) { snprintf(out, n, row == C_RESET ? "RESET TO DEFAULTS" : "BACK"); return; }
    int r = ROW_ACT(row);
    static const char *act[ACT_PER_PLAYER] = { "UP", "DOWN", "LEFT", "RIGHT", "BUTTON 1", "BUTTON 2", "BUTTON 3", "BUTTON 4", "START", "COIN" };
    if (r < 2 * ACT_PER_PLAYER) snprintf(out, n, "P%d %s", r / ACT_PER_PLAYER + 1, act[r % ACT_PER_PLAYER]);
    else if (r == ACT_SERVICE) snprintf(out, n, "SERVICE");
    else if (r == ACT_TEST) snprintf(out, n, "TEST");
}

static void crow_value(const Config *c, int row, char *out, size_t n)
{
    out[0] = 0;
    if (row < C_FIRST || row >= C_BACK) return;
    int r = ROW_ACT(row);
    const char *a = c->key[r][0] ? SDL_GetScancodeName((SDL_Scancode)c->key[r][0]) : "";
    const char *b = c->key[r][1] ? SDL_GetScancodeName((SDL_Scancode)c->key[r][1]) : "";
    char pad[48] = "";
    int ps = pad_slot(row);
    if (ps >= 0) {
        char p0[24], p1[24]; pad_bind_name(c->pad[ps][0], p0, 24); pad_bind_name(c->pad[ps][1], p1, 24);
        snprintf(pad, sizeof pad, " | PAD %s%s%s", p0, *p0 && *p1 ? " / " : "", *p0 || *p1 ? p1 : "-");
    }
    snprintf(out, n, "%s%s%s%s", a, *a && *b ? " / " : "", *a || *b ? b : "-", pad);
}

static int controls_event(Menu *m, Config *c, const SDL_Event *e)
{
    int row = m->csel, r = ROW_ACT(row);          /* r: action, valid for action rows */
    if (m->capture) {
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
            SDL_Scancode sc = e->key.scancode;
            if (sc == SDL_SCANCODE_ESCAPE) { m->capture = 0; return 0; }
            if (sc == SDL_SCANCODE_F1 || sc == SDL_SCANCODE_F10 || sc == SDL_SCANCODE_F11) return 0;   /* reserved */
            if (c->key[r][0] != (int)sc) { c->key[r][1] = c->key[r][0]; c->key[r][0] = sc; }   /* newest first, keep one more */
            if (c->key[r][1] == (int)sc) c->key[r][1] = 0;
            m->capture = 0; m->dirty = 1;
        } else if ((e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->gbutton.button != SDL_GAMEPAD_BUTTON_GUIDE) ||
                   (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && (e->gaxis.value > 24000 || e->gaxis.value < -24000))) {
            int code = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? 1 + e->gbutton.button
                                                                 : PAD_AXIS + 2 * e->gaxis.axis + (e->gaxis.value < 0);
            int ps = pad_slot(row);
            if (ps >= 0) {
                for (int j = 0; j < PAD_COUNT; j++)                     /* one input drives one action */
                    for (int k = 0; k < 2; k++) if (c->pad[j][k] == code) c->pad[j][k] = 0;
                for (int j = 0; j < PAD_COUNT; j++) if (!c->pad[j][0] && c->pad[j][1]) { c->pad[j][0] = c->pad[j][1]; c->pad[j][1] = 0; }
                c->pad[ps][1] = c->pad[ps][0]; c->pad[ps][0] = code;    /* newest first, keep one more */
                m->dirty = 1;
            }
            m->capture = 0;
            m->repeat_at = SDL_GetTicks() + 400;                        /* the stick is still held: no menu move */
        }
        return 0;
    }
    int act = 0, mv = 0, clr_keys = 0, clr_pad = 0, back = 0;
    if (e->type == SDL_EVENT_KEY_DOWN) {
        switch (e->key.key) {
        case SDLK_UP: mv = -1; break;
        case SDLK_DOWN: mv = 1; break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: if (!e->key.repeat) act = 1; break;
        case SDLK_DELETE: case SDLK_BACKSPACE: clr_keys = 1; break;
        case SDLK_ESCAPE: if (!e->key.repeat) back = 1; break;
        case SDLK_F1: if (!e->key.repeat) { m->page = 0; m->open = 0; return MENU_CH_CLOSED; } break;
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (e->gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: mv = -1; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: mv = 1; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: act = 1; break;
        case SDL_GAMEPAD_BUTTON_WEST: clr_pad = 1; break;
        case SDL_GAMEPAD_BUTTON_EAST: back = 1; break;
        case SDL_GAMEPAD_BUTTON_GUIDE: m->page = 0; m->open = 0; return MENU_CH_CLOSED;
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
        int v = e->gaxis.value, dir = v > 20000 ? 1 : v < -20000 ? -1 : 0;
        if (!dir) { m->repeat_at = 0; return 0; }
        Uint64 now = SDL_GetTicks();
        if (m->repeat_at && now < m->repeat_at) return 0;
        m->repeat_at = now + (m->repeat_at ? 120 : 350); mv = dir;
    }
    if (mv) m->csel = (m->csel + C_ROWS + mv) % C_ROWS;
    if (back) { m->page = 0; m->capture = 0; }
    if (clr_keys && row >= C_FIRST && row < C_BACK) { c->key[r][0] = c->key[r][1] = 0; m->dirty = 1; }
    if (clr_pad && pad_slot(row) >= 0) { c->pad[pad_slot(row)][0] = c->pad[pad_slot(row)][1] = 0; m->dirty = 1; }
    if (act) {
        if (row == C_RESET) { config_default_controls(c); m->dirty = 1; }
        else if (row == C_BACK) m->page = 0;
        else m->capture = 1;
    }
    return 0;
}

static void controls_draw(const Menu *m, const Config *c, SDL_Renderer *r, int px, int lh, int cw, int x0, int y0, int w, int h)
{
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    hud_text(r, x0 + cw * 2, y0 + lh / 2, px, "CONTROLS");
    int vis = I_COUNT, top = m->csel - vis / 2;
    if (top > C_ROWS - vis) top = C_ROWS - vis;
    if (top < 0) top = 0;
    for (int k = 0; k < vis && top + k < C_ROWS; k++) {
        int i = top + k, y = y0 + (k + 2) * lh;
        if (i == m->csel) {
            SDL_SetRenderDrawColor(r, m->capture ? 170 : 60, m->capture ? 110 : 90, m->capture ? 40 : 160, 255);
            SDL_FRect hl = {x0 + cw, y - px * 2, w - 2 * cw, lh}; SDL_RenderFillRect(r, &hl);
        }
        char l[32], v[64]; crow_label(i, l, sizeof l);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255); hud_text(r, x0 + cw * 2, y, px, l);
        if (i == m->csel && m->capture) snprintf(v, sizeof v, pad_slot(i) >= 0 ? "PRESS A KEY, PAD BUTTON OR STICK" : "PRESS A KEY");
        else crow_value(c, i, v, sizeof v);
        if (strlen(v) > 40) v[40] = 0;
        SDL_SetRenderDrawColor(r, 255, 220, 120, 255); hud_text(r, x0 + cw * 14, y, px, v);
    }
    SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
    hud_text(r, x0 + cw * 2, y0 + h - lh, px, m->capture ? "ESC CANCELS" : "ENTER/A SET  DEL CLEAR KEYS  X CLEAR PAD  ESC/B BACK");
}

int menu_event(Menu *m, Config *c, int *show_hud, const SDL_Event *e)
{
    if (!m->open) {
        if ((e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_F1 && !e->key.repeat) ||
            (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)) {
            m->open = 1; m->dirty = 0; m->page = 0; m->capture = 0;
        }
        if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
            (e->gbutton.button == SDL_GAMEPAD_BUTTON_START || e->gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
            SDL_Gamepad *g = SDL_GetGamepadFromID(e->gbutton.which);   /* Back + Start (XInput often hides Guide) */
            if (g && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START) && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) { m->open = 1; m->dirty = 0; m->page = 0; m->capture = 0; }
        }
        return 0;
    }
    if (m->page == 1) return controls_event(m, c, e);
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
    int w = 56 * cw, h = (I_COUNT + 4) * lh;
    int x0 = (out_w - w) / 2, y0 = (out_h - h) / 2;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 210);
    SDL_FRect bg = {x0, y0, w, h}; SDL_RenderFillRect(r, &bg);
    if (m->page == 1) { controls_draw(m, c, r, px, lh, cw, x0, y0, w, h); SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE); return; }
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    hud_text(r, x0 + cw * 2, y0 + lh / 2, px, "SETTINGS");
    SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
    hud_text(r, x0 + w - cw * (int)(sizeof("V" APP_VERSION) + 1), y0 + lh / 2, px, "V" APP_VERSION);
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
