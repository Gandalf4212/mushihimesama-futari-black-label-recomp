/* CV1000 recomp frontend (SDL2): window, display rotation, input, frame pacing.
 * Usage:
 *   <exe> --setup <rom_dir>     one-time: copy the needed ROM data into ./assets (ROM not needed after)
 *   <exe> [--yoko | --tate-cw | --tate-ccw] [--scale N] [--fullscreen] [--no-slowdown] [--rotate-controls]
 *         [--volume 0-100] [--no-vsync] [--pcb-rate] [--no-sound] [--coins] [--hud]
 * Keys: arrows move, Z X C V = buttons 1-4, 5 coin, 1 start, 2 P2 start, 6 coin 2, F2 test, 9 service,
 *       F1 settings menu, F10 performance HUD, F11 fullscreen, Esc quit. Gamepad Guide = menu. Gamepad: d-pad/left stick, A B X Y = buttons 1-4, Back coin, Start start.
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
#include "hud.h"
#include "menu.h"
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

/* assets/sound.bin: the YMZ770 ROM (u23+u24), word-swapped once here so the game loads it directly */
static int make_sound_image(const char *romdir)
{
#ifdef GAME_SND0
    CV1K *t = calloc(1, sizeof *t); char a[1024], b[1024]; int ok = 0;
    snprintf(a, sizeof a, "%s/%s", romdir, GAME_SND0); snprintf(b, sizeof b, "%s/%s", romdir, GAME_SND1);
    if (t && cv1k_load_sound(t, a, b)) ok = cv1k_save_sound_image(t, "assets/sound.bin");
    free(t);   /* the decoder state is small; the process exits or reloads from sound.bin */
    return ok;
#else
    (void)romdir; return 0;
#endif
}

static int setup(const char *romdir)
{
    char p[1024];
    MKDIR("assets");
    snprintf(p, sizeof p, "%s/%s", romdir, GAME_U4);
    if (!copy_file(p, "assets/program.bin")) return 1;
    snprintf(p, sizeof p, "%s/%s", romdir, GAME_U2);
    if (!copy_file(p, "assets/nand.bin")) return 1;
    if (!make_sound_image(romdir)) fprintf(stderr, "sound ROMs not found: the game will be silent\n");
    printf("assets/ created. The ROM files are no longer needed to play.\n");
    return 0;
}

#define ROT_YOKO ORIENT_YOKO
#define ROT_TATE_CW ORIENT_TATE_CW
#define ROT_TATE_CCW ORIENT_TATE_CCW

/* window size for an orientation and integer scale (0 = largest that fits 90 % of the desktop) */
static void window_size(Orientation orient, int scale, int *ww, int *wh)
{
    SDL_DisplayMode dm; SDL_GetDesktopDisplayMode(0, &dm);
    int vw = orient == ORIENT_YOKO ? 240 : 320, vh = orient == ORIENT_YOKO ? 320 : 240;
    if (!scale) { scale = 1; while ((scale + 1) * vw <= dm.w * 9 / 10 && (scale + 1) * vh <= dm.h * 9 / 10) scale++; }
    *ww = orient == ORIENT_YOKO ? vh * scale * 16 / 9 : vw * scale; *wh = vh * scale;
    if (*ww < vw * scale) *ww = vw * scale;
}

int main(int argc, char **argv)
{
    long shot_frame = -1; const char *shot_path = NULL; const char *cfg_path = "config.toml"; int no_sound = 0, shot_menu = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--setup") && i + 1 < argc) return setup(argv[i + 1]);
        if (!strcmp(argv[i], "--config") && i + 1 < argc) cfg_path = argv[i + 1];
        if (!strcmp(argv[i], "--no-sound")) no_sound = 1;
        if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_frame = atol(argv[i + 1]); shot_path = argv[i + 2]; }   /* debug */
        if (!strcmp(argv[i], "--shot-menu")) shot_menu = 1;   /* debug: open the menu before the shot */
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
    {   /* sound: assets/sound.bin (made at setup; made now if an older assets/ lacks it) */
        FILE *f = fopen("assets/sound.bin", "rb");
        if (f) fclose(f); else if (!make_sound_image(GAME_SET)) make_sound_image("roms/" GAME_SET);
        if (!no_sound && !cv1k_load_sound_image(&g_m, "assets/sound.bin")) fprintf(stderr, "no assets/sound.bin: silent\n");
    }
#ifdef GAME_IDLE_PC
    cv1k_set_idle(&g_m, GAME_IDLE_PC, GAME_IDLE_RAM);   /* MAME's idle speedup (part of its timing) */
#endif
    if (!slowdown) cv1k_set_timing(&g_m, 0);
    blit_ref_init(&g_blit); g_m.blit.on_exec = on_exec;
    dispatch_init(g_m.ram_size);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    int vw = orient == ROT_YOKO ? 240 : 320, vh = orient == ROT_YOKO ? 320 : 240;
    int ww, wh; window_size(orient, scale, &ww, &wh);
    SDL_Window *win = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                                       SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | (cfg.vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
    Input in; input_init(&in);
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) input_pad_added(&in, i);

    static uint32_t fb[320 * 240];
    Hud hud; hud_init(&hud); int show_hud = cfg.hud;
    /* audio: YMZ770 output is 16 kHz stereo; SDL converts to the device format. Each frame's samples
       are queued right after the frame; the queue is kept short (low latency) by dropping a frame's
       audio when more than ~4 frames are already waiting (e.g. on displays faster than 60 Hz). */
    SDL_AudioDeviceID adev = 0;
    if (g_m.ymz) {
        SDL_AudioSpec want = {0}, have;
        want.freq = 16000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 256;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (adev) SDL_PauseAudioDevice(adev, 0); else fprintf(stderr, "audio: %s\n", SDL_GetError());
    }
    static int16_t abuf[4096 * 2];
    SH3 *s = &g_m.cpu;
    uint32_t entry = GAME_MAIN_ENTRY;
    g_m.next_vsync = cv1k_vsync_at(0);
    int running = 1, booted = 0;
    /* Pacing: game frames run on a wall clock at the arcade rate (60.024 Hz like MAME, or the board's
       60.0184 Hz), independent of the display. Presentation uses the display's vsync (no tearing): on a
       60 Hz display about one frame per refresh (one is doubled or dropped every ~40 s), on faster
       displays a frame is shown on 2-3 refreshes. Input is read right before each frame runs. */
    const double freq = (double)SDL_GetPerformanceFrequency();
    double due = (double)SDL_GetPerformanceCounter() / freq;
    Menu menu; menu_init(&menu);
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_CONTROLLERDEVICEADDED) input_pad_added(&in, e.cdevice.which);
            if (e.type == SDL_CONTROLLERDEVICEREMOVED) input_pad_removed(&in, e.cdevice.which);
            int was_open = menu.open, ch = menu_event(&menu, &cfg, &show_hud, &e);
            if (!was_open && menu.open && adev) SDL_ClearQueuedAudio(adev);          /* menu opened: game paused */
            if (ch & MENU_CH_ORIENT) orient = cfg.orientation;
            if (ch & MENU_CH_FULLSCREEN) { fullscreen = cfg.fullscreen; SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
            if ((ch & (MENU_CH_SCALE | MENU_CH_ORIENT)) && !fullscreen) { int w2, h2; window_size(orient, cfg.scale, &w2, &h2); SDL_SetWindowSize(win, w2, h2); SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED); }
            if (ch & MENU_CH_VSYNC) SDL_RenderSetVSync(ren, cfg.vsync);
            if (ch & MENU_CH_SLOWDOWN) cv1k_set_timing(&g_m, cfg.slowdown);
            if (ch & MENU_CH_QUIT) running = 0;
            if (ch & MENU_CH_CLOSED) { if (menu.dirty) config_save(&cfg, cfg_path); due = (double)SDL_GetPerformanceCounter() / freq; }
            if (was_open || menu.open || ch) continue;                               /* the menu owns the input */
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
                fullscreen = !fullscreen; cfg.fullscreen = fullscreen; SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F10) show_hud = !show_hud;
        }
        const double period = 1.0 / (cfg.rate == RATE_PCB ? CV1K_HW_FRAME_HZ : CV1K_FRAME_HZ);
        int sync_display = 0;
        if (cfg.rate == RATE_DISPLAY && cfg.vsync) {                                 /* MAME -syncrefresh style */
            SDL_DisplayMode wm; if (SDL_GetWindowDisplayMode(win, &wm) == 0 && wm.refresh_rate >= 59 && wm.refresh_rate <= 61) sync_display = 1;
        }
        /* ---- wait until the next game frame is due (sleep, then spin the last ~1.5 ms) ---- */
        for (;!sync_display;) {
            double now = (double)SDL_GetPerformanceCounter() / freq, left = due - now;
            if (left <= 0) break;
            if (left > 0.002) SDL_Delay((Uint32)((left - 0.0015) * 1000));
        }
        double now = (double)SDL_GetPerformanceCounter() / freq;
        if (now - due > 4 * period || sync_display) due = now;   /* after a stall (window drag): don't race */
        if (menu.open) due = now + period;                 /* paused: keep presenting at ~60 Hz, run nothing */
        /* ---- run every frame that is due (at most 3 per present) ---- */
        uint64_t t_emu = SDL_GetPerformanceCounter();
        for (int ran = 0; !menu.open && ran < (sync_display ? 1 : 3) && due <= now; ran++, due += period) {
            if (ran) SDL_PumpEvents();
            input_frame(&in, cfg.freeplay, cfg.rotate_controls && orient == ROT_TATE_CW, cfg.rotate_controls && orient == ROT_TATE_CCW,
                        &g_m.in.c, &g_m.in.d, &g_m.in.l, &g_m.in.f);
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
            cv1k_vsync(&g_m); g_m.next_vsync = cv1k_vsync_at(g_m.frame);
            if (g_m.ymz) {
                cv1k_sound_catchup(&g_m);
                int n = cv1k_sound_read(&g_m, abuf, 4096);
                if (cfg.volume < 100) for (int k = 0; k < 2 * n; k++) abuf[k] = (int16_t)(abuf[k] * cfg.volume / 100);
                /* the queue stays short (low latency); with wall-clock pacing it only overflows after a stall */
                if (adev && n > 0 && SDL_GetQueuedAudioSize(adev) < 6u * 267 * 4) SDL_QueueAudio(adev, abuf, (Uint32)n * 4);
            }
        }
        /* ---- present ---- */
        blit_ref_frame(&g_blit, g_m.blit.scroll_x, g_m.blit.scroll_y, fb);
        SDL_UpdateTexture(tex, NULL, fb, 320 * 4);
        t_emu = SDL_GetPerformanceCounter() - t_emu;       /* emulation + software blitter */
        vw = orient == ROT_YOKO ? 240 : 320; vh = orient == ROT_YOKO ? 320 : 240;
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
        if (show_hud) hud_draw(&hud, ren, ow, oh);
        if (menu.open) menu_draw(&menu, &cfg, show_hud, ren, ow, oh);
        if ((long)g_m.frame >= shot_frame && shot_frame >= 0 && shot_menu && !menu.open) { menu.open = 1; menu.sel = 4; }
        else if ((long)g_m.frame >= shot_frame && shot_frame >= 0) {
            SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
            SDL_SaveBMP(sf, shot_path); SDL_FreeSurface(sf); running = 0;
        }
        SDL_RenderPresent(ren);
        hud_frame(&hud, t_emu, SDL_GetPerformanceCounter());
    }
    /* save settings / high scores */
    if (g_m.rtc.dirty) { FILE *f = fopen("assets/eeprom.bin", "wb"); if (f) { fwrite(g_m.rtc.eeprom, 1, sizeof g_m.rtc.eeprom, f); fclose(f); } }
    if (g_m.nand.dirty) { FILE *f = fopen("assets/nand.bin", "wb"); if (f) { fwrite(g_m.nand.data, 1, g_m.nand.size, f); fclose(f); } }
    SDL_Quit();
    return 0;
}
