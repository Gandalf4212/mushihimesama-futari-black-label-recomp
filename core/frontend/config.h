/* Frontend configuration: config.toml next to the executable, overridden by command-line flags.
 * Supported TOML subset: [section] headers, key = value with strings, integers, true/false, # comments.
 * GPL-2.0-or-later. */
#ifndef CONFIG_H
#define CONFIG_H
typedef enum { ORIENT_YOKO, ORIENT_TATE_CW, ORIENT_TATE_CCW } Orientation;
typedef struct {
    Orientation orientation;   /* [video] orientation = "yoko" | "tate-cw" | "tate-ccw" */
    int scale;                 /* [video] scale = 0 (auto) */
    int fullscreen;            /* [video] fullscreen = false */
    int slowdown;              /* [game]  slowdown = true (original timing) */
    int freeplay;              /* [game]  freeplay = true (Start inserts a coin when needed) */
    int rotate_controls;       /* [input] rotate_controls = false */
} Config;
void config_defaults(Config *c);
int  config_load(Config *c, const char *path);              /* 0 if missing; errors printed */
int  config_args(Config *c, int argc, char **argv);         /* returns index of an unknown arg or 0 */
int  config_save(const Config *c, const char *path);
#endif
