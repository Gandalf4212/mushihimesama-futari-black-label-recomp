/* CV1000 recomp frontend (SDL3): window, display rotation, input, frame pacing.
 * Usage:
 *   <exe> --setup <rom_dir>     one-time: copy the needed ROM data into ./assets (ROM not needed after)
 *   <exe> [--yoko | --tate-cw | --tate-ccw] [--scale N] [--fullscreen] [--no-slowdown] [--rotate-controls]
 *         [--volume 0-100] [--no-vsync] [--no-sound] [--coins] [--hud]
 * Keys: arrows move, Z X C V = buttons 1-4, 5 coin, 1 start, 2 P2 start, 6 coin 2, F2 test, 9 service,
 *       F1 settings menu, F10 performance HUD, F11 fullscreen, Esc quit. Gamepad Guide = menu. Gamepad: d-pad/left stick, A B X Y = buttons 1-4, Back coin, Start start.
 * GPL-2.0-or-later. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
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
#include "present.h"
#include "romfind.h"
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
    const SDL_DisplayMode *m = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    SDL_DisplayMode dm = m ? *m : (SDL_DisplayMode){.w = 1920, .h = 1080, .pixel_density = 1};
    if (dm.pixel_density > 0) { dm.w = (int)(dm.w * dm.pixel_density); dm.h = (int)(dm.h * dm.pixel_density); }   /* pixels */
    int vw = orient == ORIENT_YOKO ? 240 : 320, vh = orient == ORIENT_YOKO ? 320 : 240;
    if (!scale) { scale = 1; while ((scale + 1) * vw <= dm.w * 9 / 10 && (scale + 1) * vh <= dm.h * 9 / 10) scale++; }
    *ww = orient == ORIENT_YOKO ? vh * scale * 16 / 9 : vw * scale; *wh = vh * scale;
    if (*ww < vw * scale) *ww = vw * scale;
}

/* Window sizes are asked for in pixels. With display scaling (e.g. 125 % on a 1440p screen) SDL may use
   scaled window coordinates: correct the size so the window really has ww x wh pixels. */
static void fit_window_pixels(SDL_Window *win, int ww, int wh)
{
    int pw, ph, w, h;
    if (!SDL_GetWindowSizeInPixels(win, &pw, &ph) || !SDL_GetWindowSize(win, &w, &h) || pw <= 0 || ph <= 0) return;
    if (pw != ww || ph != wh) SDL_SetWindowSize(win, (int)((double)ww * w / pw + 0.5), (int)((double)wh * h / ph + 0.5));
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
    else {                                       /* first run: build assets/ from the ROM set (folder, zip, 7z or picker) */
        SDL_Init(SDL_INIT_VIDEO);
        if (rom_setup(setup) != 0) return 1;
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

    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");   /* small device buffer: low latency */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    int ww, wh; window_size(orient, scale, &ww, &wh);
    SDL_Window *win = SDL_CreateWindow(GAME_TITLE, ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (!win) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    fit_window_pixels(win, ww, wh);
    Presenter pr;
    if (!present_init(&pr, win, &cfg)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = pr.ren;
    Input in; input_init(&in);
    /* pads already connected arrive as SDL_EVENT_GAMEPAD_ADDED */

    static uint32_t fb[320 * 240];
    Hud hud; hud_init(&hud); int show_hud = cfg.hud; hud.backend = present_backend(&pr);
    /* audio: YMZ770 output is 16 kHz stereo; SDL converts to the device format. Each frame's samples
       are queued right after the frame; the queue is kept short (low latency) by dropping a frame's
       audio when more than ~4 frames are already waiting (e.g. on displays faster than 60 Hz). */
    SDL_AudioStream *adev = NULL;
    if (g_m.ymz) {
        SDL_AudioSpec want = {SDL_AUDIO_S16, 2, 16000};
        adev = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);
        if (adev) SDL_ResumeAudioStreamDevice(adev); else fprintf(stderr, "audio: %s\n", SDL_GetError());
    }
    static int16_t abuf[4096 * 2];
    SH3 *s = &g_m.cpu;
    uint32_t entry = GAME_MAIN_ENTRY;
    g_m.next_vsync = cv1k_vsync_at(0);
    int running = 1, booted = 0;
    /* Pacing: game frames run on a wall clock at the arcade rate (60.024 Hz, the native rate
       measured on a PCB, same as MAME), independent of the display. Presentation uses the display's vsync (no tearing): on a
       60 Hz display about one frame per refresh (one is doubled or dropped every ~40 s), on faster
       displays a frame is shown on 2-3 refreshes. Input is read right before each frame runs. */
    const double freq = (double)SDL_GetPerformanceFrequency();
    double due = (double)SDL_GetPerformanceCounter() / freq;
    Menu menu; menu_init(&menu);
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED) input_pad_added(&in, e.gdevice.which);
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED) input_pad_removed(&in, e.gdevice.which);
            int was_open = menu.open, ch = menu_event(&menu, &cfg, &show_hud, &e);
            if (!was_open && menu.open && adev) SDL_ClearAudioStream(adev);          /* menu opened: game paused */
            if (ch & MENU_CH_ORIENT) orient = cfg.orientation;
            if (ch & MENU_CH_FULLSCREEN) { fullscreen = cfg.fullscreen; SDL_SetWindowFullscreen(win, fullscreen != 0); }
            if ((ch & (MENU_CH_SCALE | MENU_CH_ORIENT)) && !fullscreen) { int w2, h2; window_size(orient, cfg.scale, &w2, &h2); SDL_SetWindowSize(win, w2, h2); fit_window_pixels(win, w2, h2); SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED); }
            if (ch & MENU_CH_VSYNC) SDL_SetRenderVSync(ren, cfg.vsync && !cfg.uncapped ? 1 : 0);
            if (ch & MENU_CH_SLOWDOWN) cv1k_set_timing(&g_m, cfg.slowdown);
            if (ch & MENU_CH_QUIT) running = 0;
            if (ch & MENU_CH_CLOSED) { if (menu.dirty) config_save(&cfg, cfg_path); due = (double)SDL_GetPerformanceCounter() / freq; }
            if (was_open || menu.open || ch) continue;                               /* the menu owns the input */
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = 0;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F11) {
                fullscreen = !fullscreen; cfg.fullscreen = fullscreen; SDL_SetWindowFullscreen(win, fullscreen != 0);
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_F10) show_hud = !show_hud;
        }
        const double period = 1.0 / CV1K_FRAME_HZ;
        int sync_display = 0;
        if (cfg.rate == RATE_DISPLAY && cfg.vsync) {                                 /* MAME -syncrefresh style */
            const SDL_DisplayMode *wm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win)); if (wm && wm->refresh_rate >= 59 && wm->refresh_rate <= 61) sync_display = 1;
        }
        /* ---- wait until the next game frame is due (sleep, then spin the last ~1.5 ms) ---- */
        for (;!sync_display && !cfg.uncapped;) {
            double now = (double)SDL_GetPerformanceCounter() / freq, left = due - now;
            if (left <= 0) break;
            if (left > 0.002) SDL_Delay((Uint32)((left - 0.0015) * 1000));
        }
        double now = (double)SDL_GetPerformanceCounter() / freq;
        if (now - due > 4 * period || sync_display) due = now;   /* after a stall (window drag): don't race */
        if (menu.open) due = now + period;                 /* paused: keep presenting at ~60 Hz, run nothing */
        /* ---- run every frame that is due (at most 3 per present) ---- */
        uint64_t t_emu = SDL_GetPerformanceCounter();
        int ran = 0;
        for (ran = 0; !menu.open && ran < (sync_display ? 1 : 3) && due <= now; ran++, due += period) {
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
                if (adev && n > 0 && SDL_GetAudioStreamQueued(adev) < 6 * 267 * 4) SDL_PutAudioStreamData(adev, abuf, n * 4);
            }
        }
        /* ---- present ---- */
        if (ran) blit_ref_frame(&g_blit, g_m.blit.scroll_x, g_m.blit.scroll_y, fb);   /* only when the game advanced */
        t_emu = SDL_GetPerformanceCounter() - t_emu;       /* emulation + software blitter */
        uint64_t t_pres = SDL_GetPerformanceCounter();
        int ow, oh; SDL_GetCurrentRenderOutputSize(ren, &ow, &oh);
        present_draw(&pr, &cfg, orient, ran ? fb : NULL, ow, oh);
        hud.note = cfg.uncapped ? "UNCAPPED" : NULL;
        if (show_hud) hud_draw(&hud, ren, ow, oh);
        if (menu.open) menu_draw(&menu, &cfg, show_hud, ren, ow, oh);
        if ((long)g_m.frame >= shot_frame && shot_frame >= 0 && shot_menu && !menu.open) { menu.open = 1; menu.sel = 4; }
        else if ((long)g_m.frame >= shot_frame && shot_frame >= 0) {
            SDL_Surface *sf = SDL_RenderReadPixels(ren, NULL);
            if (sf) { SDL_SaveBMP(sf, shot_path); SDL_DestroySurface(sf); }
            running = 0;
        }
        SDL_RenderPresent(ren);
        uint64_t t_end = SDL_GetPerformanceCounter();
        hud_frame(&hud, ran, ran ? t_emu : 0, t_end - t_pres, t_end);
    }
    /* save settings / high scores */
    if (g_m.rtc.dirty) { FILE *f = fopen("assets/eeprom.bin", "wb"); if (f) { fwrite(g_m.rtc.eeprom, 1, sizeof g_m.rtc.eeprom, f); fclose(f); } }
    if (g_m.nand.dirty) { FILE *f = fopen("assets/nand.bin", "wb"); if (f) { fwrite(g_m.nand.data, 1, g_m.nand.size, f); fclose(f); } }
    SDL_Quit();
    return 0;
}
