/* Input: 2 players on keyboard (MAME default keys + alternates) and up to 2 gamepads; free play.
 * GPL-2.0-or-later. */
#include "input.h"
#include <string.h>

/* bit layout of CV1000 ports (active low on the board; we build active-high then invert) */
enum { UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8, B1 = 0x10, B2 = 0x20, B3 = 0x40, B4 = 0x80 };
enum { C_SERVICE = 0x01, C_COIN1 = 0x04, C_COIN2 = 0x08, C_START1 = 0x10, C_START2 = 0x20 };

void input_init(Input *in) { memset(in, 0, sizeof *in); }

void input_pad_added(Input *in, SDL_JoystickID id)
{
    /* SDL3 sends SDL_EVENT_GAMEPAD_ADDED for every pad, including those connected at startup; ignore a
       pad that is already open, or one pad would drive P1 and P2. */
    for (int p = 0; p < 2; p++) if (in->pad[p] && SDL_GetGamepadID(in->pad[p]) == id) return;
    for (int p = 0; p < 2; p++) if (!in->pad[p]) { in->pad[p] = SDL_OpenGamepad(id); return; }
}

void input_pad_removed(Input *in, SDL_JoystickID id)
{
    for (int p = 0; p < 2; p++)
        if (in->pad[p] && SDL_GetGamepadID(in->pad[p]) == id) { SDL_CloseGamepad(in->pad[p]); in->pad[p] = NULL; }
}

static uint8_t pad_bits(SDL_Gamepad *g, const Config *c, int *start, int *coin)
{
    if (!g) return 0;
    int ax = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), ay = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    uint8_t b = 0;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) || ay < -16000) b |= UP;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ay > 16000) b |= DOWN;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || ax < -16000) b |= LEFT;
    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || ax > 16000) b |= RIGHT;
#define PB(i) (c->pad[i] >= 0 && SDL_GetGamepadButton(g, (SDL_GamepadButton)c->pad[i]))
    if (PB(PAD_B1)) b |= B1;
    if (PB(PAD_B2)) b |= B2;
    if (PB(PAD_B3)) b |= B3;
    if (PB(PAD_B4)) b |= B4;
    *start |= PB(PAD_START);
    *coin |= PB(PAD_COIN);
#undef PB
    return b;
}

static uint8_t rotate_dirs(uint8_t b, int tate_cw, int tate_ccw)
{
    if (!tate_cw && !tate_ccw) return b;
    uint8_t u = b & UP, d = b & DOWN, l = b & LEFT, r = b & RIGHT, o = b & 0xF0;
    if (tate_cw) return o | (l ? UP : 0) | (r ? DOWN : 0) | (d ? LEFT : 0) | (u ? RIGHT : 0);
    return o | (r ? UP : 0) | (l ? DOWN : 0) | (u ? LEFT : 0) | (d ? RIGHT : 0);
}

void input_frame(Input *in, const Config *cfg, int rot_cw, int rot_ccw, uint8_t *port_c, uint8_t *port_d, uint8_t *port_l, uint8_t *port_f)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    int freeplay = cfg->freeplay;
#define DOWN_(a) ((cfg->key[a][0] && k[cfg->key[a][0]]) || (cfg->key[a][1] && k[cfg->key[a][1]]))
    uint8_t pl[2]; int st[2], co[2];
    for (int p = 0; p < 2; p++) {
        int o = p * ACT_PER_PLAYER;
        pl[p] = (DOWN_(o + ACT_UP) ? UP : 0) | (DOWN_(o + ACT_DOWN) ? DOWN : 0) | (DOWN_(o + ACT_LEFT) ? LEFT : 0) | (DOWN_(o + ACT_RIGHT) ? RIGHT : 0)
              | (DOWN_(o + ACT_B1) ? B1 : 0) | (DOWN_(o + ACT_B2) ? B2 : 0) | (DOWN_(o + ACT_B3) ? B3 : 0) | (DOWN_(o + ACT_B4) ? B4 : 0);
        st[p] = DOWN_(o + ACT_START); co[p] = DOWN_(o + ACT_COIN);
    }
    uint8_t p1 = pl[0], p2 = pl[1];
    int st1 = st[0], st2 = st[1], coin1 = co[0], coin2 = co[1];
    p1 |= pad_bits(in->pad[0], cfg, &st1, &coin1);
    p2 |= pad_bits(in->pad[1], cfg, &st2, &coin2);
    p1 = rotate_dirs(p1, rot_cw, rot_ccw);
    p2 = rotate_dirs(p2, rot_cw, rot_ccw);

    /* free play: a Start press first inserts a coin (held 4 frames), then presses Start (4 frames) */
    uint8_t c = 0;
    for (int p = 0; p < 2; p++) {
        int st = p ? st2 : st1;
        if (freeplay && st && !in->start_held[p] && !in->fp_timer[p]) in->fp_timer[p] = 12;
        in->start_held[p] = st;
        if (in->fp_timer[p]) {
            int t = in->fp_timer[p]--;
            if (t > 8) c |= p ? C_COIN2 : C_COIN1;      /* frames 12..9: coin */
            else if (t <= 4) c |= p ? C_START2 : C_START1; /* frames 4..1: start */
        } else if (st) c |= p ? C_START2 : C_START1;
    }
    if (coin1) c |= C_COIN1;
    if (coin2) c |= C_COIN2;
    if (DOWN_(ACT_SERVICE)) c |= C_SERVICE;
    *port_c = (uint8_t)~c; *port_d = (uint8_t)~p1; *port_l = (uint8_t)~p2;
    *port_f = (uint8_t)~(DOWN_(ACT_TEST) ? 0x02 : 0);
#undef DOWN_
}
