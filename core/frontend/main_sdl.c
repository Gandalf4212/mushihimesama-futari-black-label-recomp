/* CV1000 recomp frontend (SDL2): window, display rotation, input, frame pacing.
 * Usage:
 *   <exe> --setup <rom_dir>     one-time: copy the needed ROM data into ./assets (ROM not needed after)
 *   <exe> [--yoko | --tate-cw | --tate-ccw] [--scale N] [--fullscreen] [--no-slowdown] [--rotate-controls]
 * Keys: arrows move, Z X C V = buttons 1-4, 5 coin, 1 start, 2 P2 start, 6 coin 2, F2 test, 9 service,
 *       F11 fullscreen, Esc quit. Gamepad: d-pad/left stick, A B X Y = buttons 1-4, Back coin, Start start.
 * GPL-2.0-or-later. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0755)
#endif
#include "../runtime/cv1k.h"
#include "../runtime/dispatch.h"
#include "../render/blit_ref.h"
#include "game_config.h"
#include "config.h"
#include "input.h"   /* generated per game: GAME_SET, GAME_TITLE, GAME_RAM_SIZE, GAME_MAIN_ENTRY, GAME_U4, GAME_U2 */

static BlitRef g_blit;
static CV1K g_m;
static void on_exec(void *ctx, uint32_t list) { (void)ctx; blit_ref_exec(&g_blit, g_m.ram, g_m.ram_size - 1, list, g_m.blit.clip_x, g_m.blit.clip_y); }

static int copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"); if (!a) { fprintf(stderr, "missing %s\n", from); return 0; }
    FILE *b = fopen(to, "wb"); if (!b) { fclose(a); fprintf(stderr, "cannot write %s\n", to); return 0; }
    char buf[1 << 16]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a); fclose(b); return 1;
}

static int setup(const char *romdir)
{
    char p[1024];
    MKDIR("assets");
    snprintf(p, sizeof p, "%s/%s", romdir, GAME_U4);
    if (!copy_file(p, "assets/program.bin")) return 1;
    snprintf(p, sizeof p, "%s/%s", romdir, GAME_U2);
    if (!copy_file(p, "assets/nand.bin")) return 1;
    printf("assets/ created. The ROM files are no longer needed to play.\n");
    return 0;
}

#define ROT_YOKO ORIENT_YOKO
#define ROT_TATE_CW ORIENT_TATE_CW
#define ROT_TATE_CCW ORIENT_TATE_CCW

int main(int argc, char **argv)
{
    long shot_frame = -1; const char *shot_path = NULL; const char *cfg_path = "config.toml";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--setup") && i + 1 < argc) return setup(argv[i + 1]);
        if (!strcmp(argv[i], "--config") && i + 1 < argc) cfg_path = argv[i + 1];
        if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_frame = atol(argv[i + 1]); shot_path = argv[i + 2]; }   /* debug */
    }
    Config cfg; config_defaults(&cfg);
    if (!config_load(&cfg, cfg_path)) config_save(&cfg, cfg_path);   /* first run: write the defaults */
    int bad = config_args(&cfg, argc, argv);
    if (bad && strcmp(argv[bad], "--setup")) fprintf(stderr, "unknown option %s\n", argv[bad]);
    Orientation orient = cfg.orientation; int scale = cfg.scale, fullscreen = cfg.fullscreen, slowdown = cfg.slowdown;
    FILE *probe = fopen("assets/program.bin", "rb");
    if (probe) fclose(probe);
    else if (setup(GAME_SET) != 0 && setup("roms/" GAME_SET) != 0) {   /* first run: build assets/ from the ROM */
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_TITLE,
            "First run: put your " GAME_SET " ROM files (" GAME_U4 ", " GAME_U2 ") in a folder named \""
            GAME_SET "\" next to this program, then start it again.\nAfter that the ROM files are no longer needed.", NULL);
        return 1;
    }
    if (!cv1k_init(&g_m, "assets/program.bin", "assets/nand.bin", GAME_RAM_SIZE)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_TITLE, "Could not load assets/. Delete the assets folder and start again.", NULL);
        return 1;
    }
    {   /* saved EEPROM (settings, high scores) from previous runs */
        FILE *f = fopen("assets/eeprom.bin", "rb");
        if (f) { if (fread(g_m.rtc.eeprom, 1, sizeof g_m.rtc.eeprom, f) != sizeof g_m.rtc.eeprom) fprintf(stderr, "short eeprom.bin\n"); fclose(f); }
    }
    if (!slowdown) cv1k_set_timing(&g_m, 0);
    blit_ref_init(&g_blit); g_m.blit.on_exec = on_exec;
    dispatch_init(g_m.ram_size);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_DisplayMode dm; SDL_GetDesktopDisplayMode(0, &dm);
    /* native buffer is 320x240 landscape; ROT270 game -> upright view is the buffer rotated 270 deg clockwise */
    int vw = orient == ROT_YOKO ? 240 : 320, vh = orient == ROT_YOKO ? 320 : 240;
    if (!scale) { scale = 1; while ((scale + 1) * vw <= dm.w * 9 / 10 && (scale + 1) * vh <= dm.h * 9 / 10) scale++; }
    int ww = orient == ROT_YOKO ? vh * scale * 16 / 9 : vw * scale, wh = vh * scale;
    if (ww < vw * scale) ww = vw * scale;
    SDL_Window *win = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                                       SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
    Input in; input_init(&in);
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) input_pad_added(&in, i);

    static uint32_t fb[320 * 240];
    SH3 *s = &g_m.cpu;
    uint32_t entry = GAME_MAIN_ENTRY;
    g_m.next_vsync = g_m.cycles_per_frame;
    int running = 1, booted = 0;
    while (running) {
        /* ---- input: read right before the frame runs ---- */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                fullscreen = !fullscreen; SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            }
            if (e.type == SDL_CONTROLLERDEVICEADDED) input_pad_added(&in, e.cdevice.which);
            if (e.type == SDL_CONTROLLERDEVICEREMOVED) input_pad_removed(&in, e.cdevice.which);
        }
        input_frame(&in, cfg.freeplay, cfg.rotate_controls && orient == ROT_TATE_CW, cfg.rotate_controls && orient == ROT_TATE_CCW,
                    &g_m.in.c, &g_m.in.d, &g_m.in.l, &g_m.in.f);

        /* ---- run exactly one frame (until the next vsync) ---- */
        while (s->cycles < g_m.next_vsync) {               /* same order as run_headless (verified vs MAME) */
            cv1k_check_irq(&g_m);
            { uint64_t te = cv1k_next_event(&g_m); s->event_at = te < g_m.next_vsync ? te : g_m.next_vsync; }
            if (s->sleeping) { s->cycles = s->event_at; cv1k_timers(&g_m); continue; }
            if (!booted && (s->pc & 0x1FFFFFFFu) == (entry & 0x1FFFFFFFu)) booted = 1;
            if (!booted) sh3_step(s);                        /* the game's own boot code, on the interpreter */
            else dispatch_step(s);
            if (s->trap) cv1k_take_trap(&g_m);
            cv1k_timers(&g_m);
        }
        cv1k_vsync(&g_m); g_m.next_vsync += g_m.cycles_per_frame;

        /* ---- present ---- */
        blit_ref_frame(&g_blit, g_m.blit.scroll_x, g_m.blit.scroll_y, fb);
        SDL_UpdateTexture(tex, NULL, fb, 320 * 4);
        int ow, oh; SDL_GetRendererOutputSize(ren, &ow, &oh);
        int sc = 1; while ((sc + 1) * vw <= ow && (sc + 1) * vh <= oh) sc++;   /* integer scale */
        int dw = vw * sc, dh = vh * sc;
        SDL_Rect dst;
        double angle = orient == ROT_YOKO ? 270.0 : orient == ROT_TATE_CW ? 180.0 : 0.0;
        if (orient == ROT_YOKO) {                            /* texture is 320x240 before rotating 90 deg */
            dst.w = dh; dst.h = dw; dst.x = (ow - dst.w) / 2; dst.y = (oh - dst.h) / 2;
        } else { dst.w = dw; dst.h = dh; dst.x = (ow - dw) / 2; dst.y = (oh - dh) / 2; }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopyEx(ren, tex, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
        if ((long)g_m.frame == shot_frame) {
            SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
            SDL_SaveBMP(sf, shot_path); SDL_FreeSurface(sf); running = 0;
        }
        SDL_RenderPresent(ren);
    }
    /* save settings / high scores */
    if (g_m.rtc.dirty) { FILE *f = fopen("assets/eeprom.bin", "wb"); if (f) { fwrite(g_m.rtc.eeprom, 1, sizeof g_m.rtc.eeprom, f); fclose(f); } }
    if (g_m.nand.dirty) { FILE *f = fopen("assets/nand.bin", "wb"); if (f) { fwrite(g_m.nand.data, 1, g_m.nand.size, f); fclose(f); } }
    SDL_Quit();
    return 0;
}
