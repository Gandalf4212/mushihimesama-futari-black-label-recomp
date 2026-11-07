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

static int pad_on(SDL_Gamepad *g, int code)
{
    if (code >= PAD_AXIS) {
        int a = (code - PAD_AXIS) / 2, v = SDL_GetGamepadAxis(g, (SDL_GamepadAxis)a);
        return (code - PAD_AXIS) & 1 ? v < -16000 : v > 16000;
    }
    return code > 0 && SDL_GetGamepadButton(g, (SDL_GamepadButton)(code - 1));
}

static uint8_t pad_bits(SDL_Gamepad *g, const Config *c, int *start, int *coin)
{
    if (!g) return 0;
    static const uint8_t bit[ACT_PER_PLAYER] = { UP, DOWN, LEFT, RIGHT, B1, B2, B3, B4, 0, 0 };
    uint8_t b = 0;
    for (int a = 0; a < ACT_PER_PLAYER; a++) {
        if (!pad_on(g, c->pad[a][0]) && !pad_on(g, c->pad[a][1])) continue;
        if (a == ACT_START) *start = 1; else if (a == ACT_COIN) *coin = 1; else b |= bit[a];
    }
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

void input_rumble(Input *in, int player, int level)
{
    if (player < 0 || player > 1 || !in->pad[player] || level <= 0) return;
    if (level >= 2) SDL_RumbleGamepad(in->pad[player], 0xFFFF, 0xC000, 450);
    else SDL_RumbleGamepad(in->pad[player], 0x7000, 0x3800, 300);
}
