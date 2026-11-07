/* First-run builder (Windows): makes assets/game.dll from the player's own ROM, so the release ships none
 * of the game's code (docs/builder-plan.md). Steps: boot the program ROM on the interpreter -> RAM image;
 * translate it to C (core/sh3/recomp.c) in assets/_build; compile the files in parallel with the bundled
 * toolchain (toolchain/bin/gcc.exe, w64devkit) and the runtime headers in recomp/; link game.dll.
 * Shows a progress screen in the ROM-check style; runs without one if no window can be opened.
 * GPL-2.0-or-later. */
#include "builder.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_config.h"
#include "hud.h"
#include "../sh3/recomp.h"

#ifdef _WIN32
#include <windows.h>
#define BOOT_RUNNER_NO_MAIN
#include "../runtime/boot_runner.c"

#define TC_GCC   "toolchain/bin/gcc.exe"
#define TC_B     "-Btoolchain/bin/"
#define BUILD    "assets/_build"
#define MAXF     256

static int exists(const char *p) { SDL_PathInfo pi; return SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_FILE; }
int game_build_available(void) { return exists(TC_GCC) && exists("recomp/sh3/sh3.h") && exists("recomp/runtime/recomp_dll.c"); }

/* ---- screen ---- */
typedef struct { SDL_Window *w; SDL_Renderer *r; int W, H; Uint64 t0; } Screen;
enum { ST_WAIT, ST_RUN, ST_OK, ST_FAIL };
static const char *step_name[4] = { "BOOT THE PROGRAM ROM", "TRANSLATE TO C", "COMPILE", "LINK GAME.DLL" };

static void fill(SDL_Renderer *r, float x, float y, float w, float h, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, a); SDL_FRect f = { x, y, w, h }; SDL_RenderFillRect(r, &f);
}

static int draw(Screen *sc, const int st[4], int done, int total, int jobs, const char *msg)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return 0;
    if (!sc->r) return 1;
    SDL_Renderer *r = sc->r; int W = sc->W, H = sc->H;
    float t = (SDL_GetTicks() - sc->t0) / 1000.f;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int y = 0; y < H; y += 4) { float k = (float)y / H; fill(r, 0, (float)y, (float)W, 4, (Uint8)(14 + 10 * k), (Uint8)(10 + 4 * k), (Uint8)(30 + 22 * (1 - k)), 255); }
    int px = H >= 900 ? 4 : H >= 700 ? 3 : 2, lh = 12 * px, cw = 6 * px;
    float x0 = W * 0.08f, y0 = H * 0.12f, tw = W * 0.84f;
    SDL_SetRenderDrawColor(r, 255, 200, 90, 255); hud_text(r, (int)x0, (int)y0, px + 1, "BUILDING GAME CODE");
    SDL_SetRenderDrawColor(r, 170, 170, 200, 255); hud_text(r, (int)x0, (int)(y0 + 13 * (px + 1)), px, "ONE TIME ONLY: COMPILED FROM YOUR ROM ON THIS PC");
    float sweep = SDL_fmodf(t * 0.35f, 1.0f);
    fill(r, x0, y0 + 13 * (px + 1) + lh, tw, (float)px, 60, 50, 110, 255);
    fill(r, x0 + tw * sweep * 0.8f, y0 + 13 * (px + 1) + lh, tw * 0.2f, (float)px, 255, 200, 90, 255);
    float ty = y0 + 13 * (px + 1) + lh * 2.4f;
    char b[128];
    for (int i = 0; i < 4; i++) {
        float y = ty + lh * 1.25f * i;
        fill(r, x0 - cw, y - px * 2.5f, tw + 2 * cw, lh, 255, 255, 255, i & 1 ? 10 : 22);
        SDL_SetRenderDrawColor(r, 235, 235, 245, 255); hud_text(r, (int)x0, (int)y, px, step_name[i]);
        float sx = x0 + 28 * cw;
        if (i == 2 && st[i] == ST_RUN) {
            fill(r, sx, y, 20 * cw, 7 * px, 60, 60, 90, 255); fill(r, sx, y, 20 * cw * (total ? (float)done / total : 0), 7 * px, 255, 200, 90, 255);
            snprintf(b, sizeof b, "%d / %d  (%d AT ONCE)", done, total, jobs);
            SDL_SetRenderDrawColor(r, 220, 220, 235, 255); hud_text(r, (int)(sx + 21 * cw), (int)y, px, b);
        } else if (st[i] == ST_RUN) { SDL_SetRenderDrawColor(r, 255, 200, 90, 255); hud_text(r, (int)sx, (int)y, px, ((int)(t * 3) & 1) ? "WORKING" : "WORKING."); }
        else if (st[i] == ST_OK) { fill(r, sx - px, y - px, 6 * cw, 9 * px, 20, 120, 50, 255); SDL_SetRenderDrawColor(r, 120, 255, 140, 255); hud_text(r, (int)sx, (int)y, px, "DONE"); }
        else if (st[i] == ST_FAIL) { fill(r, sx - px, y - px, 8 * cw, 9 * px, 130, 20, 30, 255); SDL_SetRenderDrawColor(r, 255, 110, 110, 255); hud_text(r, (int)sx, (int)y, px, "FAILED"); }
        else { SDL_SetRenderDrawColor(r, 110, 110, 130, 255); hud_text(r, (int)sx, (int)y, px, "-"); }
    }
    float fy = ty + lh * 1.25f * 4 + lh;
    int m = (int)t / 60, s = (int)t % 60;
    snprintf(b, sizeof b, "ELAPSED %d:%02d", m, s);
    SDL_SetRenderDrawColor(r, 170, 170, 200, 255); hud_text(r, (int)x0, (int)fy, px, b);
    if (msg) { SDL_SetRenderDrawColor(r, 220, 220, 235, 255); hud_text(r, (int)x0, (int)(fy + lh * 1.5f), px, msg); }
    SDL_RenderPresent(r);
    return 1;
}

/* ---- processes ---- */
typedef struct { HANDLE proc; int file; } Job;

static HANDLE start(const char *cmd, const char *logpath)
{
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE log = CreateFileA(logpath, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = NULL; si.hStdOutput = log; si.hStdError = log;
    PROCESS_INFORMATION pi;
    char line[4096]; snprintf(line, sizeof line, "%s", cmd);
    BOOL ok = CreateProcessA(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) return NULL;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

/* 1 = still running; else 0 and *code set */
static int running(HANDLE h, DWORD *code)
{
    if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT) return 1;
    GetExitCodeProcess(h, code); CloseHandle(h);
    return 0;
}

static void append_log(const char *from, FILE *to)
{
    FILE *f = fopen(from, "rb"); if (!f) return;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) fwrite(buf, 1, n, to);
    fclose(f);
}

static void clean_build_dir(void)
{
    int n = 0; char **list = SDL_GlobDirectory(BUILD, NULL, 0, &n);
    for (int i = 0; list && i < n; i++) { char p[512]; snprintf(p, sizeof p, BUILD "/%s", list[i]); SDL_RemovePath(p); }
    SDL_free(list);
    SDL_RemovePath(BUILD);
}

int game_build(char *why, size_t wn)
{
    why[0] = 0;
    Screen sc = {0}; sc.t0 = SDL_GetTicks();
    if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        sc.W = dm ? dm->w * 6 / 10 : 1152; sc.H = sc.W * 9 / 16;
        sc.w = SDL_CreateWindow(GAME_TITLE " - building", sc.W, sc.H, 0);
        sc.r = sc.w ? SDL_CreateRenderer(sc.w, NULL) : NULL;
        if (sc.r) { SDL_SetRenderVSync(sc.r, 1); SDL_GetCurrentRenderOutputSize(sc.r, &sc.W, &sc.H); }
    }
    int st[4] = { ST_RUN, ST_WAIT, ST_WAIT, ST_WAIT }, ok = 0, done = 0, nf = 0, jobs = 1, fail_step = -1;
    FILE *blog = fopen("assets/build.log", "w");
    draw(&sc, st, 0, 0, 0, NULL);
    clean_build_dir(); SDL_CreateDirectory(BUILD);

    /* 1. boot */
    uint8_t *ram = malloc(GAME_RAM_SIZE);
    {
        SDL_IOStream *f = SDL_IOFromFile("assets/program.bin", "rb");
        size_t n = 0; void *rom = f ? SDL_LoadFile_IO(f, &n, true) : NULL;
        int r = rom && boot_run_image(rom, (uint32_t)n, GAME_RAM_SIZE, GAME_MAIN_ENTRY, ram);
        SDL_free(rom);
        if (!r) { snprintf(why, wn, "the program ROM did not boot"); fail_step = 0; goto end; }
    }
    st[0] = ST_OK; st[1] = ST_RUN; draw(&sc, st, 0, 0, 0, NULL);

    /* 2. translate */
    {
        RecompCfg c = {0};
        c.main_entry = GAME_MAIN_ENTRY; c.vbr = GAME_VBR; c.vector_addr = GAME_VEC_ADDR; c.vector_size = GAME_VEC_SIZE;
        c.extra_path = exists("recomp/hints.txt") ? "recomp/hints.txt" : NULL;
        char msg[256];
        if (!recomp_translate(ram, GAME_RAM_SIZE, &c, BUILD, &nf, msg, sizeof msg)) { snprintf(why, wn, "translation failed: %s", msg); fail_step = 1; goto end; }
        if (blog) fprintf(blog, "translate: %s\n", msg);
    }
    free(ram); ram = NULL;
    st[1] = ST_OK; st[2] = ST_RUN;

    /* 3. compile: recomp_NNN.c + recomp_table.c + the runtime glue, in parallel; jobs limited by cores and
       memory (the largest file needs ~1.8 GB in cc1) */
    {
        int total = nf + 2;
        int cores = SDL_GetNumLogicalCPUCores(), ram_mb = SDL_GetSystemRAM();
        jobs = (ram_mb - 2048) / 1800; if (jobs > cores) jobs = cores; if (jobs < 1) jobs = 1; if (jobs > 16) jobs = 16;
        if (blog) fprintf(blog, "compile: %d files, %d jobs (%d cores, %d MB)\n", total, jobs, cores, ram_mb);
        Job run[16]; int nrun = 0, next = 0;
        while (done < total) {
            while (nrun < jobs && next < total) {
                char cmd[1024], log[256];
                snprintf(log, sizeof log, BUILD "/%d.log", next);
                if (next < nf) snprintf(cmd, sizeof cmd, TC_GCC " " TC_B " -O1 -w -Irecomp/sh3 -c " BUILD "/recomp_%03d.c -o " BUILD "/recomp_%03d.o", next, next);
                else if (next == nf) snprintf(cmd, sizeof cmd, TC_GCC " " TC_B " -O1 -w -Irecomp/sh3 -c " BUILD "/recomp_table.c -o " BUILD "/recomp_table.o");
                else snprintf(cmd, sizeof cmd, TC_GCC " " TC_B " -O2 -w -DRECOMP_U4_CRC=0x%08Xu -c recomp/runtime/recomp_dll.c -o " BUILD "/recomp_dll.o", (unsigned)GAME_U4_CRC);
                HANDLE h = start(cmd, log);
                if (!h) { snprintf(why, wn, "could not start the compiler (%s)", TC_GCC); fail_step = 2; break; }
                run[nrun].proc = h; run[nrun].file = next; nrun++; next++;
            }
            if (fail_step >= 0) break;
            for (int i = 0; i < nrun; i++) {
                DWORD code;
                if (running(run[i].proc, &code)) continue;
                if (code != 0) {
                    char log[256]; snprintf(log, sizeof log, BUILD "/%d.log", run[i].file);
                    if (blog) { fprintf(blog, "file %d failed (exit %lu):\n", run[i].file, (unsigned long)code); append_log(log, blog); }
                    snprintf(why, wn, "a file did not compile (see assets/build.log)"); fail_step = 2;
                }
                run[i--] = run[--nrun]; done++;
            }
            if (fail_step >= 0) break;
            if (!draw(&sc, st, done, total, jobs, "THIS TAKES A FEW MINUTES. THE GAME STARTS BY ITSELF.")) { snprintf(why, wn, "cancelled"); fail_step = 2; break; }
            SDL_Delay(50);
        }
        if (fail_step >= 0) { for (int i = 0; i < nrun; i++) { TerminateProcess(run[i].proc, 1); CloseHandle(run[i].proc); } goto end; }
    }
    st[2] = ST_OK; st[3] = ST_RUN; draw(&sc, st, done, done, jobs, NULL);

    /* 4. link */
    {
        char line[8192] = TC_GCC " " TC_B " -shared -o " BUILD "/game.dll ";   /* no shell: list the objects */
        for (int i = 0; i < nf; i++) { size_t l = strlen(line); snprintf(line + l, sizeof line - l, BUILD "/recomp_%03d.o ", i); }
        { size_t l = strlen(line); snprintf(line + l, sizeof line - l, BUILD "/recomp_table.o " BUILD "/recomp_dll.o -static-libgcc -Wl,--exclude-all-symbols"); }
        HANDLE h = start(line, BUILD "/link.log");
        DWORD code = 1;
        if (h) while (running(h, &code)) { draw(&sc, st, done, done, jobs, NULL); SDL_Delay(50); }
        if (!h || code != 0) {
            if (blog) { fprintf(blog, "link failed:\n"); append_log(BUILD "/link.log", blog); }
            snprintf(why, wn, "linking failed (see assets/build.log)"); fail_step = 3; goto end;
        }
        SDL_RemovePath("assets/game.dll");
        if (!SDL_RenamePath(BUILD "/game.dll", "assets/game.dll")) { snprintf(why, wn, "could not write assets/game.dll"); fail_step = 3; goto end; }
    }
    st[3] = ST_OK; ok = 1;

end:
    free(ram);
    if (fail_step >= 0) {
        st[fail_step] = ST_FAIL;
        if (blog) fprintf(blog, "FAILED: %s\n", why);
        Uint64 t = SDL_GetTicks();
        while (SDL_GetTicks() - t < 1500) { draw(&sc, st, done, nf + 2, jobs, why); SDL_Delay(30); }
    } else if (blog) fprintf(blog, "done in %.0f s\n", (SDL_GetTicks() - sc.t0) / 1000.0);
    if (blog) fclose(blog);
    if (ok) clean_build_dir();          /* keep the files after a failure, for the log */
    if (sc.r) SDL_DestroyRenderer(sc.r);
    if (sc.w) SDL_DestroyWindow(sc.w);
    return ok;
}
#else
int game_build_available(void) { return 0; }
int game_build(char *why, size_t wn) { snprintf(why, wn, "not needed on this platform"); return 0; }
#endif
