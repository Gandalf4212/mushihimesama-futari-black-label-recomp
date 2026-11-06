/* Input: 2 players on keyboard (MAME default keys + alternates) and up to 2 gamepads; free play.
 * GPL-2.0-or-later. */
#include "input.h"
#include <string.h>

/* bit layout of CV1000 ports (active low on the board; we build active-high then invert) */
enum { UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8, B1 = 0x10, B2 = 0x20, B3 = 0x40, B4 = 0x80 };
enum { C_SERVICE = 0x01, C_COIN1 = 0x04, C_COIN2 = 0x08, C_START1 = 0x10, C_START2 = 0x20 };

void input_init(Input *in) { memset(in, 0, sizeof *in); }

void input_pad_added(Input *in, int device_index)
{
    /* SDL sends CONTROLLERDEVICEADDED at startup for pads that are already connected, and main_sdl.c
       also opens them explicitly: ignore a pad that is already open, or one pad would drive P1 and P2. */
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device_index);
    for (int p = 0; p < 2; p++)
        if (in->pad[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(in->pad[p])) == id) return;
    for (int p = 0; p < 2; p++) if (!in->pad[p]) { in->pad[p] = SDL_GameControllerOpen(device_index); return; }
}

void input_pad_removed(Input *in, SDL_JoystickID id)
{
    for (int p = 0; p < 2; p++)
        if (in->pad[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(in->pad[p])) == id) {
            SDL_GameControllerClose(in->pad[p]); in->pad[p] = NULL;
        }
}

static uint8_t pad_bits(SDL_GameController *g, int *start, int *coin)
{
    if (!g) return 0;
    int ax = SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTY);
    uint8_t b = 0;
    if (SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -16000) b |= UP;
    if (SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > 16000) b |= DOWN;
    if (SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -16000) b |= LEFT;
    if (SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 16000) b |= RIGHT;
    if (SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_A)) b |= B1;
    if (SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_B)) b |= B2;
    if (SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_X)) b |= B3;
    if (SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_Y)) b |= B4;
    *start |= SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_START);
    *coin |= SDL_GameControllerGetButton(g, SDL_CONTROLLER_BUTTON_BACK);
    return b;
}

static uint8_t rotate_dirs(uint8_t b, int tate_cw, int tate_ccw)
{
    if (!tate_cw && !tate_ccw) return b;
    uint8_t u = b & UP, d = b & DOWN, l = b & LEFT, r = b & RIGHT, o = b & 0xF0;
    if (tate_cw) return o | (l ? UP : 0) | (r ? DOWN : 0) | (d ? LEFT : 0) | (u ? RIGHT : 0);
    return o | (r ? UP : 0) | (l ? DOWN : 0) | (u ? LEFT : 0) | (d ? RIGHT : 0);
}

void input_frame(Input *in, int freeplay, int rot_cw, int rot_ccw, uint8_t *port_c, uint8_t *port_d, uint8_t *port_l, uint8_t *port_f)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    /* P1: MAME defaults (arrows, LCtrl LAlt Space LShift) + alternates Z X C V */
    uint8_t p1 = (k[SDL_SCANCODE_UP] ? UP : 0) | (k[SDL_SCANCODE_DOWN] ? DOWN : 0) | (k[SDL_SCANCODE_LEFT] ? LEFT : 0) | (k[SDL_SCANCODE_RIGHT] ? RIGHT : 0)
               | (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_Z] ? B1 : 0) | (k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_X] ? B2 : 0)
               | (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_C] ? B3 : 0) | (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_V] ? B4 : 0);
    /* P2: MAME defaults R F D G, A S Q W */
    uint8_t p2 = (k[SDL_SCANCODE_R] ? UP : 0) | (k[SDL_SCANCODE_F] ? DOWN : 0) | (k[SDL_SCANCODE_D] ? LEFT : 0) | (k[SDL_SCANCODE_G] ? RIGHT : 0)
               | (k[SDL_SCANCODE_A] ? B1 : 0) | (k[SDL_SCANCODE_S] ? B2 : 0) | (k[SDL_SCANCODE_Q] ? B3 : 0) | (k[SDL_SCANCODE_W] ? B4 : 0);
    int st1 = k[SDL_SCANCODE_1], st2 = k[SDL_SCANCODE_2], coin1 = k[SDL_SCANCODE_5], coin2 = k[SDL_SCANCODE_6];
    p1 |= pad_bits(in->pad[0], &st1, &coin1);
    p2 |= pad_bits(in->pad[1], &st2, &coin2);
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
    if (k[SDL_SCANCODE_9]) c |= C_SERVICE;
    *port_c = (uint8_t)~c; *port_d = (uint8_t)~p1; *port_l = (uint8_t)~p2;
    *port_f = (uint8_t)~(k[SDL_SCANCODE_F2] ? 0x02 : 0);
}
