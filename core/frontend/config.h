/* Frontend configuration: config.toml next to the executable, overridden by command-line flags.
 * Supported TOML subset: [section] headers, key = value with strings, integers, true/false, # comments.
 * GPL-2.0-or-later. */
#ifndef CONFIG_H
#define CONFIG_H
typedef enum { ORIENT_YOKO, ORIENT_TATE_CW, ORIENT_TATE_CCW } Orientation;

/* Control bindings. Actions per player (P2 = P1 + ACT_PER_PLAYER), then the cabinet switches. */
enum { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_B1, ACT_B2, ACT_B3, ACT_B4, ACT_START, ACT_COIN, ACT_PER_PLAYER };
enum { ACT_SERVICE = 2 * ACT_PER_PLAYER, ACT_TEST, ACT_COUNT };
/* Gamepad buttons (both pads; directions are always d-pad + left stick) */
enum { PAD_B1, PAD_B2, PAD_B3, PAD_B4, PAD_START, PAD_COIN, PAD_COUNT };
extern const char *const act_key_name[ACT_COUNT];     /* config keys: p1_up ... service, test */
extern const char *const pad_key_name[PAD_COUNT];     /* config keys: pad_b1 ... pad_coin */
typedef struct {
    Orientation orientation;   /* [video] orientation = "yoko" | "tate-cw" | "tate-ccw" */
    int scale;                 /* [video] scale = 0 (auto) */
    int fullscreen;            /* [video] fullscreen = false */
    int hud;                   /* [video] hud = false (performance overlay, F10 toggles) */
    int slowdown;              /* [game]  slowdown = true (original timing) */
    int freeplay;              /* [game]  freeplay = true (Start inserts a coin when needed) */
    int rotate_controls;       /* [input] rotate_controls = false */
    int vsync;                 /* [video] vsync = true (present on the display's vsync; pacing is separate) */
    int rate;                  /* [game]  frame_rate = "native" (60.024 Hz, measured on a PCB; MAME's rate) |
                                  "display" (one game frame per refresh on a ~60 Hz display, like MAME -syncrefresh) */
    int volume;                /* [audio] volume = 100 (0-100) */
    int scaling;               /* [video] scaling = "fit" (largest, keeps aspect) | "integer" */
    int shader;                /* [video] shader = "none" | "sharp" | "crt" | "crt-curved" */
    int bezel;                 /* [video] bezel = 0 off | 1 ours (procedural) | 2.. artwork view 1.. (Yoko only) */
    char artwork[260];         /* [video] artwork = "" : a MAME .lay (bezel pack) or a PNG overlay; empty = look
                                  for Bezel/default.lay or artwork/default.lay next to the program */
    int artwork_views;         /* runtime: views in the loaded .lay (not saved) */
    int uncapped;              /* [video] uncapped = false (benchmark: present as fast as possible; game stays 60.024 Hz) */
    int key[ACT_COUNT][2];     /* [input] p1_up = "Up" ... : up to two SDL scancodes per action (0 = none) */
    int pad[PAD_COUNT];        /* [input] pad_b1 = "a" ... : SDL gamepad button (-1 = none) */
} Config;
void config_defaults(Config *c);
void config_default_controls(Config *c);
enum { RATE_NATIVE, RATE_DISPLAY };
enum { SCALING_FIT, SCALING_INTEGER };
enum { SHADER_NONE, SHADER_SHARP, SHADER_CRT, SHADER_CURVED };
int  config_load(Config *c, const char *path);              /* 0 if missing; errors printed */
int  config_args(Config *c, int argc, char **argv);         /* returns index of an unknown arg or 0 */
int  config_save(const Config *c, const char *path);
#endif
