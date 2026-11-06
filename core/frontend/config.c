/* Minimal TOML-subset config reader/writer. GPL-2.0-or-later. */
#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Config *c)
{
    c->orientation = ORIENT_YOKO; c->scale = 0; c->fullscreen = 0; c->hud = 0;
    c->slowdown = 1; c->freeplay = 1; c->rotate_controls = 0;
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
        else if (!strcmp(a, "--config") || !strcmp(a, "--shot")) i += !strcmp(a, "--shot") ? 2 : 1;   /* handled elsewhere */
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
    fprintf(f, "[video]\norientation = \"%s\"   # yoko | tate-cw | tate-ccw\nscale = %d             # 0 = largest that fits\nfullscreen = %s\nhud = %s             # performance overlay (F10 toggles)\n\n",
            o[c->orientation], c->scale, c->fullscreen ? "true" : "false", c->hud ? "true" : "false");
    fprintf(f, "[game]\nslowdown = %s          # original slowdown\nfreeplay = %s          # Start inserts a coin when needed\n\n",
            c->slowdown ? "true" : "false", c->freeplay ? "true" : "false");
    fprintf(f, "[input]\nrotate_controls = %s   # rotate stick directions with a Tate display\n", c->rotate_controls ? "true" : "false");
    fclose(f);
    return 1;
}
