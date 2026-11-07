/* Minimal TOML-subset config reader/writer. GPL-2.0-or-later. */
#include "config.h"
#include <stdio.h>
#include <SDL3/SDL.h>

const char *const act_key_name[ACT_COUNT] = {
    "p1_up", "p1_down", "p1_left", "p1_right", "p1_b1", "p1_b2", "p1_b3", "p1_b4", "p1_start", "p1_coin",
    "p2_up", "p2_down", "p2_left", "p2_right", "p2_b1", "p2_b2", "p2_b3", "p2_b4", "p2_start", "p2_coin",
    "service", "test" };
const char *const pad_key_name[PAD_COUNT] = { "pad_up", "pad_down", "pad_left", "pad_right", "pad_b1", "pad_b2", "pad_b3",
                                              "pad_b4", "pad_start", "pad_coin" };

int pad_bind_parse(const char *s)
{
    while (*s == ' ') s++;
    if (*s == '+' || *s == '-') {
        SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(s + 1);
        return a == SDL_GAMEPAD_AXIS_INVALID ? 0 : PAD_AXIS + 2 * a + (*s == '-');
    }
    SDL_GamepadButton b = SDL_GetGamepadButtonFromString(s);
    return b == SDL_GAMEPAD_BUTTON_INVALID ? 0 : 1 + b;
}

void pad_bind_name(int code, char *out, int n)
{
    const char *s = NULL;
    if (code >= PAD_AXIS) s = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)((code - PAD_AXIS) / 2));
    else if (code > 0) s = SDL_GetGamepadStringForButton((SDL_GamepadButton)(code - 1));
    if (!s) { out[0] = 0; return; }
    snprintf(out, (size_t)n, "%s%s", code >= PAD_AXIS ? ((code - PAD_AXIS) & 1 ? "-" : "+") : "", s);
}

/* MAME's default keys, plus Z X C V for P1's buttons */
void config_default_controls(Config *c)
{
    static const int k[ACT_COUNT][2] = {
        {SDL_SCANCODE_UP}, {SDL_SCANCODE_DOWN}, {SDL_SCANCODE_LEFT}, {SDL_SCANCODE_RIGHT},
        {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z}, {SDL_SCANCODE_LALT, SDL_SCANCODE_X}, {SDL_SCANCODE_SPACE, SDL_SCANCODE_C},
        {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_V}, {SDL_SCANCODE_1}, {SDL_SCANCODE_5},
        {SDL_SCANCODE_R}, {SDL_SCANCODE_F}, {SDL_SCANCODE_D}, {SDL_SCANCODE_G},
        {SDL_SCANCODE_A}, {SDL_SCANCODE_S}, {SDL_SCANCODE_Q}, {SDL_SCANCODE_W}, {SDL_SCANCODE_2}, {SDL_SCANCODE_6},
        {SDL_SCANCODE_9}, {SDL_SCANCODE_F2} };
    memcpy(c->key, k, sizeof k);
#define B_(x) (1 + SDL_GAMEPAD_BUTTON_##x)
#define A_(x, neg) (PAD_AXIS + 2 * SDL_GAMEPAD_AXIS_##x + (neg))
    static const int p[PAD_COUNT][2] = {
        {B_(DPAD_UP), A_(LEFTY, 1)}, {B_(DPAD_DOWN), A_(LEFTY, 0)}, {B_(DPAD_LEFT), A_(LEFTX, 1)}, {B_(DPAD_RIGHT), A_(LEFTX, 0)},
        {B_(SOUTH)}, {B_(EAST)}, {B_(WEST)}, {B_(NORTH)}, {B_(START)}, {B_(BACK)} };
#undef B_
#undef A_
    memcpy(c->pad, p, sizeof p);
}
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Config *c)
{
    c->orientation = ORIENT_YOKO; c->scale = 0; c->fullscreen = 0; c->hud = 0;
    c->slowdown = 1; c->freeplay = 1; c->rotate_controls = 0; c->vsync = 1; c->rate = RATE_NATIVE; c->volume = 100;
    config_default_controls(c); c->rumble = 1;
    c->scaling = SCALING_FIT; c->shader = SHADER_SHARP; c->bezel = 1; c->artwork[0] = 0; c->artwork_views = 0; c->uncapped = 0;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int as_bool(const char *v) { return !strcmp(v, "true") || !strcmp(v, "1") || !strcmp(v, "yes"); }

static void set(Config *c, const char *sec, const char *key, const char *v)
{
    char full[128]; snprintf(full, sizeof full, "%s.%s", sec, key);
    if (!strcmp(full, "video.orientation")) {
        c->orientation = !strcmp(v, "tate-cw") ? ORIENT_TATE_CW : !strcmp(v, "tate-ccw") ? ORIENT_TATE_CCW : ORIENT_YOKO;
    } else if (!strcmp(full, "video.scale")) c->scale = atoi(v);
    else if (!strcmp(full, "video.fullscreen")) c->fullscreen = as_bool(v);
    else if (!strcmp(full, "video.hud")) c->hud = as_bool(v);
    else if (!strcmp(full, "game.slowdown")) c->slowdown = as_bool(v);
    else if (!strcmp(full, "game.freeplay")) c->freeplay = as_bool(v);
    else if (!strcmp(full, "input.rotate_controls")) c->rotate_controls = as_bool(v);
    else if (!strcmp(full, "input.rumble")) { c->rumble = !strcmp(v, "true") ? 1 : !strcmp(v, "false") ? 0 : atoi(v); if (c->rumble < 0 || c->rumble > 2) c->rumble = 1; }
    else if (!strcmp(full, "video.vsync")) c->vsync = as_bool(v);
    else if (!strcmp(full, "game.frame_rate")) c->rate = !strcmp(v, "display") ? RATE_DISPLAY : RATE_NATIVE;   /* old "mame"/"pcb" -> native */
    else if (!strcmp(full, "audio.volume")) { c->volume = atoi(v); if (c->volume < 0) c->volume = 0; if (c->volume > 100) c->volume = 100; }
    else if (!strcmp(full, "video.scaling")) c->scaling = !strcmp(v, "integer") ? SCALING_INTEGER : SCALING_FIT;
    else if (!strcmp(full, "video.shader")) c->shader = !strcmp(v, "none") ? SHADER_NONE : !strcmp(v, "crt") ? SHADER_CRT : !strcmp(v, "crt-curved") ? SHADER_CURVED : SHADER_SHARP;
    else if (!strcmp(full, "video.bezel")) c->bezel = !strcmp(v, "true") ? 1 : !strcmp(v, "false") ? 0 : atoi(v);
    else if (!strcmp(full, "video.artwork") || !strcmp(full, "video.bezel_image")) snprintf(c->artwork, sizeof c->artwork, "%s", v);
    else if (!strcmp(full, "video.uncapped")) c->uncapped = as_bool(v);
    else if (!strncmp(full, "input.", 6)) {
        const char *k = full + 6;
        for (int i = 0; i < ACT_COUNT; i++) if (!strcmp(k, act_key_name[i])) {   /* "Left Ctrl, Z" */
            char buf[128]; snprintf(buf, sizeof buf, "%s", v);
            c->key[i][0] = c->key[i][1] = 0; int n = 0;
            for (char *t = strtok(buf, ","); t && n < 2; t = strtok(NULL, ",")) {
                while (*t == ' ') t++;
                char *e = t + strlen(t); while (e > t && e[-1] == ' ') *--e = 0;
                SDL_Scancode sc = SDL_GetScancodeFromName(t);
                if (sc != SDL_SCANCODE_UNKNOWN) c->key[i][n++] = sc;
            }
            return;
        }
        for (int i = 0; i < PAD_COUNT; i++) if (!strcmp(k, pad_key_name[i])) {      /* "dpup, -lefty" */
            char buf[128]; snprintf(buf, sizeof buf, "%s", v);
            c->pad[i][0] = c->pad[i][1] = 0; int n = 0;
            for (char *t = strtok(buf, ","); t && n < 2; t = strtok(NULL, ",")) { int code = pad_bind_parse(t); if (code) c->pad[i][n++] = code; }
            return;
        }
        fprintf(stderr, "config: unknown key %s\n", full);
    }
    else fprintf(stderr, "config: unknown key %s\n", full);
}

int config_load(Config *c, const char *path)
{
    FILE *f = fopen(path, "r"); if (!f) return 0;
    char line[512], sec[64] = "";
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *s = trim(line);
        if (!*s) continue;
        if (*s == '[') { char *e = strchr(s, ']'); if (e) { *e = 0; snprintf(sec, sizeof sec, "%s", trim(s + 1)); } continue; }
        char *eq = strchr(s, '='); if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);
        if (*v == '"') { v++; char *q = strchr(v, '"'); if (q) *q = 0; }
        set(c, sec, k, v);
    }
    fclose(f);
    return 1;
}

int config_args(Config *c, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--yoko")) c->orientation = ORIENT_YOKO;
        else if (!strcmp(a, "--tate-cw")) c->orientation = ORIENT_TATE_CW;
        else if (!strcmp(a, "--tate-ccw")) c->orientation = ORIENT_TATE_CCW;
        else if (!strcmp(a, "--scale") && i + 1 < argc) c->scale = atoi(argv[++i]);
        else if (!strcmp(a, "--fullscreen")) c->fullscreen = 1;
        else if (!strcmp(a, "--windowed")) c->fullscreen = 0;
        else if (!strcmp(a, "--hud")) c->hud = 1;
        else if (!strcmp(a, "--no-hud")) c->hud = 0;
        else if (!strcmp(a, "--no-sound")) ;   /* handled in main_sdl.c */
        else if (!strcmp(a, "--no-slowdown")) c->slowdown = 0;
        else if (!strcmp(a, "--slowdown")) c->slowdown = 1;
        else if (!strcmp(a, "--freeplay")) c->freeplay = 1;
        else if (!strcmp(a, "--coins")) c->freeplay = 0;
        else if (!strcmp(a, "--rotate-controls")) c->rotate_controls = 1;
        else if (!strcmp(a, "--no-rumble")) c->rumble = 0;
        else if (!strcmp(a, "--no-vsync")) c->vsync = 0;
        else if (!strcmp(a, "--sync-display")) c->rate = RATE_DISPLAY;
        else if (!strcmp(a, "--uncapped")) c->uncapped = 1;
        else if (!strcmp(a, "--integer")) c->scaling = SCALING_INTEGER;
        else if (!strcmp(a, "--no-bezel")) c->bezel = 0;
        else if (!strcmp(a, "--artwork") && i + 1 < argc) snprintf(c->artwork, sizeof c->artwork, "%s", argv[++i]);
        else if (!strcmp(a, "--shader") && i + 1 < argc) { const char *v = argv[++i]; c->shader = !strcmp(v, "none") ? SHADER_NONE : !strcmp(v, "crt") ? SHADER_CRT : !strcmp(v, "crt-curved") ? SHADER_CURVED : SHADER_SHARP; }
        else if (!strcmp(a, "--volume") && i + 1 < argc) { c->volume = atoi(argv[++i]); if (c->volume < 0) c->volume = 0; if (c->volume > 100) c->volume = 100; }
        else if (!strcmp(a, "--config")) i++;   /* handled in main */
        else if (!strcmp(a, "--set") && i + 1 < argc) {        /* --set section.key=value */
            char buf[256]; snprintf(buf, sizeof buf, "%s", argv[++i]);
            char *dot = strchr(buf, '.'), *eq = strchr(buf, '=');
            if (dot && eq && dot < eq) { *dot = 0; *eq = 0; set(c, buf, dot + 1, eq + 1); }
        } else return i;
    }
    return 0;
}

int config_save(const Config *c, const char *path)
{
    FILE *f = fopen(path, "w"); if (!f) return 0;
    static const char *o[] = {"yoko", "tate-cw", "tate-ccw"};
    fprintf(f, "# Settings. Command-line flags override these (see README.txt).\n\n");
    fprintf(f, "[video]\norientation = \"%s\"   # yoko | tate-cw | tate-ccw\nscale = %d             # 0 = largest that fits\nfullscreen = %s\nhud = %s             # performance overlay (F10 toggles)\nvsync = %s           # tear-free presentation; game speed is paced separately\n"
               "scaling = \"%s\"      # fit (largest, keeps aspect) | integer\nshader = \"%s\"       # none | sharp | crt | crt-curved\n"
               "bezel = %d            # Yoko: 0 off, 1 ours, 2 = artwork view 1, 3 = view 2, ...\nartwork = \"%s\"       # MAME .lay bezel pack or PNG/JPG overlay (empty: Bezel/default.lay if present)\n"
               "uncapped = %s        # benchmark: present as fast as possible (game still runs at 60.024 Hz)\n\n",
            o[c->orientation], c->scale, c->fullscreen ? "true" : "false", c->hud ? "true" : "false", c->vsync ? "true" : "false",
            c->scaling == SCALING_INTEGER ? "integer" : "fit", (const char *[]){"none", "sharp", "crt", "crt-curved"}[c->shader],
            c->bezel, c->artwork, c->uncapped ? "true" : "false");
    fprintf(f, "[game]\nslowdown = %s          # original slowdown\nfreeplay = %s          # Start inserts a coin when needed\nframe_rate = \"%s\"      # native (60.024 Hz) | display (1 frame per refresh on ~60 Hz)\n\n[audio]\nvolume = %d            # 0-100\n\n",
            c->slowdown ? "true" : "false", c->freeplay ? "true" : "false", c->rate == RATE_DISPLAY ? "display" : "native", c->volume);
    fprintf(f, "[input]\nrotate_controls = %s   # rotate stick directions with a Tate display\n", c->rotate_controls ? "true" : "false");
    fprintf(f, "rumble = %d              # gamepad rumble when that player loses a life: 0 off, 1 low, 2 high\n", c->rumble);
    fprintf(f, "# keys: up to two per action, SDL key names; F1 menu > CONTROLS changes them\n");
    for (int i = 0; i < ACT_COUNT; i++) {
        const char *k0 = c->key[i][0] ? SDL_GetScancodeName((SDL_Scancode)c->key[i][0]) : "";
        const char *k1 = c->key[i][1] ? SDL_GetScancodeName((SDL_Scancode)c->key[i][1]) : "";
        fprintf(f, "%s = \"%s%s%s\"\n", act_key_name[i], k0, *k0 && *k1 ? ", " : "", k1);
    }
    fprintf(f, "# gamepad (both pads), two per action: buttons a b x y back start leftshoulder rightshoulder dpup ...,\n"
               "# stick / trigger directions -leftx +leftx -lefty +lefty -rightx ... +lefttrigger +righttrigger\n");
    for (int i = 0; i < PAD_COUNT; i++) {
        char a0[32], a1[32]; pad_bind_name(c->pad[i][0], a0, 32); pad_bind_name(c->pad[i][1], a1, 32);
        fprintf(f, "%s = \"%s%s%s\"\n", pad_key_name[i], a0, *a0 && *a1 ? ", " : "", a1);
    }
    fclose(f);
    return 1;
}
