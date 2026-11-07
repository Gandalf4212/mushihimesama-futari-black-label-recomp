#ifndef INPUT_H
#define INPUT_H
#include <SDL3/SDL.h>
#include <stdint.h>
#include "config.h"
typedef struct {
    SDL_Gamepad *pad[2];
    int start_held[2], fp_timer[2];
} Input;
void input_init(Input *in);
void input_pad_added(Input *in, SDL_JoystickID id);
void input_pad_removed(Input *in, SDL_JoystickID id);
/* Build CV1000 port values (active low) for this frame. */
void input_frame(Input *in, const Config *cfg, int rot_cw, int rot_ccw, uint8_t *port_c, uint8_t *port_d, uint8_t *port_l, uint8_t *port_f);
#endif
