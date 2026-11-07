/* First run: find the ROM set and build assets/ from it.
 * Looks for <set>/ and roms/<set>/ folders, then <set>.zip / <set>.7z (also under roms/), and otherwise asks
 * the user to pick a ROM archive or folder. Zip archives are read with our own reader + inflate (stored
 * and deflate entries; MAME sets use these). 7z archives are unpacked with Windows' own tar.exe
 * (libarchive, Windows 10 1803+); if that fails the user is asked for a zip or folder instead.
 * GPL-2.0-or-later. */
#include "romfind.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include "game_config.h"
#include "hud.h"
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

#define TMPDIR "assets/_rom"

static const char *wanted[] = { GAME_U4, GAME_U2,
#ifdef GAME_SND0
    GAME_SND0, GAME_SND1,
#endif
};
#define NWANTED ((int)(sizeof wanted / sizeof wanted[0]))

static int exists(const char *p) { SDL_PathInfo pi; return SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_FILE; }

/* diagnostics on stderr (what was tried and why it failed) */
static void logf_(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}

void rom_chdir_base(void)
{
    const char *bp = SDL_GetBasePath(); if (!bp) return;
#ifdef _WIN32
    wchar_t w[1024]; if (MultiByteToWideChar(CP_UTF8, 0, bp, -1, w, 1024)) _wchdir(w);
#else
    if (chdir(bp)) {}
#endif
}

/* ---------------- inflate (RFC 1951), our own ---------------- */
typedef struct { const uint8_t *in; size_t inlen, ip; uint32_t bb; int bc; uint8_t *out; size_t outlen, op; int err; } Inf;
typedef struct { uint16_t count[16], sym[320]; } Huff;

static int getbits(Inf *s, int n)
{
    while (s->bc < n) {
        if (s->ip >= s->inlen) { s->err = 1; return 0; }
        s->bb |= (uint32_t)s->in[s->ip++] << s->bc; s->bc += 8;
    }
    int v = (int)(s->bb & ((1u << n) - 1)); s->bb >>= n; s->bc -= n; return v;
}

static void build(Huff *h, const uint8_t *len, int n)
{
    uint16_t offs[16]; memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0; offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
}

static int decode(Inf *s, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= getbits(s, 1);
        int count = h->count[len];
        if (code - count < first) return h->sym[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
        if (s->err) return -1;
    }
    s->err = 1; return -1;
}

static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t  lext[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t  dext[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int codes(Inf *s, const Huff *lit, const Huff *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0 || s->err) return 0;
        if (sym < 256) { if (s->op >= s->outlen) return 0; s->out[s->op++] = (uint8_t)sym; continue; }
        if (sym == 256) return 1;
        sym -= 257; if (sym >= 29) return 0;
        size_t len = lbase[sym] + getbits(s, lext[sym]);
        int ds = decode(s, dist); if (ds < 0 || ds >= 30) return 0;
        size_t d = dbase[ds] + getbits(s, dext[ds]);
        if (s->err || d > s->op || s->op + len > s->outlen) return 0;
        uint8_t *o = s->out + s->op; for (size_t i = 0; i < len; i++) o[i] = o[(ptrdiff_t)i - (ptrdiff_t)d];
        s->op += len;
    }
}

static int inflate_all(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen)
{
    Inf s = { in, inlen, 0, 0, 0, out, outlen, 0, 0 };
    static Huff lit, dist; uint8_t len[320];
    int last;
    do {
        last = getbits(&s, 1);
        int type = getbits(&s, 2);
        if (s.err) return 0;
        if (type == 0) {                                     /* stored */
            s.bb = 0; s.bc = 0;
            if (s.ip + 4 > s.inlen) return 0;
            size_t n = s.in[s.ip] | (size_t)s.in[s.ip + 1] << 8; s.ip += 4;
            if (s.ip + n > s.inlen || s.op + n > s.outlen) return 0;
            memcpy(s.out + s.op, s.in + s.ip, n); s.ip += n; s.op += n;
        } else if (type == 1) {                              /* fixed codes */
            int i = 0;
            for (; i < 144; i++) len[i] = 8;
            for (; i < 256; i++) len[i] = 9;
            for (; i < 280; i++) len[i] = 7;
            for (; i < 288; i++) len[i] = 8;
            build(&lit, len, 288);
            for (i = 0; i < 30; i++) len[i] = 5;
            build(&dist, len, 30);
            if (!codes(&s, &lit, &dist)) return 0;
        } else if (type == 2) {                              /* dynamic codes */
            static const uint8_t ord[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            int nlen = getbits(&s, 5) + 257, ndist = getbits(&s, 5) + 1, ncode = getbits(&s, 4) + 4;
            if (s.err || nlen > 286 || ndist > 30) return 0;
            memset(len, 0, sizeof len);
            for (int i = 0; i < ncode; i++) len[ord[i]] = (uint8_t)getbits(&s, 3);
            Huff cl; build(&cl, len, 19);
            int i = 0;
            while (i < nlen + ndist) {
                int sym = decode(&s, &cl); if (sym < 0) return 0;
                if (sym < 16) { len[i++] = (uint8_t)sym; continue; }
                int rep, val = 0;
                if (sym == 16) { if (!i) return 0; val = len[i - 1]; rep = 3 + getbits(&s, 2); }
                else if (sym == 17) rep = 3 + getbits(&s, 3);
                else rep = 11 + getbits(&s, 7);
                if (i + rep > nlen + ndist) return 0;
                while (rep--) len[i++] = (uint8_t)val;
            }
            build(&lit, len, nlen);
            build(&dist, len + nlen, ndist);
            if (!codes(&s, &lit, &dist)) return 0;
        } else return 0;
    } while (!last);
    return s.op == outlen;
}

/* ---------------- zip ---------------- */
static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static const char *base_name(const char *p)
{
    const char *b = p;
    for (; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

static int rd_at(SDL_IOStream *f, Sint64 off, void *buf, size_t n)
{
    return SDL_SeekIO(f, off, SDL_IO_SEEK_SET) == off && SDL_ReadIO(f, buf, n) == n;
}

static int zip_extract(const char *arc, const char *outdir)
{
    SDL_IOStream *f = SDL_IOFromFile(arc, "rb");
    if (!f) { logf_("zip %s: cannot open (%s)", arc, SDL_GetError()); return 0; }
    Sint64 sz = SDL_GetIOSize(f);
    Sint64 tail = sz < 66000 ? sz : 66000;
    uint8_t *t = malloc((size_t)tail);
    if (!t || !rd_at(f, sz - tail, t, (size_t)tail)) { logf_("zip %s: read error (size %lld)", arc, (long long)sz); free(t); SDL_CloseIO(f); return 0; }
    Sint64 eocd = -1;
    for (Sint64 i = tail - 22; i >= 0; i--) if (rd32(t + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { logf_("zip %s: no end-of-directory record", arc); free(t); SDL_CloseIO(f); return 0; }
    uint32_t nent = rd16(t + eocd + 10), cdsize = rd32(t + eocd + 12), cdoff = rd32(t + eocd + 16);
    free(t);
    logf_("zip %s: %u entries, directory %u bytes at %u", arc, nent, cdsize, cdoff);
    uint8_t *cd = malloc(cdsize ? cdsize : 1);
    if (!cd || !rd_at(f, cdoff, cd, cdsize)) { logf_("zip: cannot read the directory"); free(cd); SDL_CloseIO(f); return 0; }
    int got = 0;
    for (uint32_t i = 0, p = 0; i < nent && p + 46 <= cdsize; i++) {
        if (rd32(cd + p) != 0x02014b50) { logf_("zip: bad directory entry %u", i); break; }
        uint32_t flags = rd16(cd + p + 8), method = rd16(cd + p + 10), csize = rd32(cd + p + 20), usize = rd32(cd + p + 24);
        uint32_t nl = rd16(cd + p + 28), el = rd16(cd + p + 30), cl = rd16(cd + p + 32), loff = rd32(cd + p + 42);
        char name[256]; size_t k = nl < 255 ? nl : 255; memcpy(name, cd + p + 46, k); name[k] = 0;
        p += 46 + nl + el + cl;
        int want = -1;
        for (int w = 0; w < NWANTED; w++) if (!SDL_strcasecmp(base_name(name), wanted[w])) want = w;
        logf_("  %s: method %u flags %x packed %u size %u%s", name, method, flags, csize, usize, want < 0 ? " (not needed)" : "");
        if (want < 0) continue;
        if ((method != 0 && method != 8) || (flags & 1)) { logf_("  unsupported (method %u%s)", method, flags & 1 ? ", encrypted" : ""); continue; }
        uint8_t lh[30];
        if (!rd_at(f, loff, lh, 30) || rd32(lh) != 0x04034b50) { logf_("  bad local header"); continue; }
        Sint64 data = (Sint64)loff + 30 + rd16(lh + 26) + rd16(lh + 28);
        uint8_t *in = malloc(csize ? csize : 1), *out = method == 0 ? in : malloc(usize ? usize : 1);
        int ok = in && out && rd_at(f, data, in, csize);
        if (!ok) logf_("  read failed");
        else if (method == 8 && !inflate_all(in, csize, out, usize)) { ok = 0; logf_("  inflate failed"); }
        if (ok) {
            char op[1024]; snprintf(op, sizeof op, "%s/%s", outdir, wanted[want]);
            SDL_IOStream *o = SDL_IOFromFile(op, "wb");
            ok = o && SDL_WriteIO(o, out, usize) == usize;
            if (o) SDL_CloseIO(o);
            if (!ok) logf_("  cannot write %s (%s)", op, SDL_GetError());
            got += ok;
        }
        if (out != in) free(out);
        free(in);
    }
    free(cd); SDL_CloseIO(f);
    logf_("zip: extracted %d of %d", got, NWANTED);
    return got;
}

/* ---------------- 7z (Windows tar.exe) ---------------- */
static int sevenz_extract(const char *arc, const char *outdir)
{
#ifdef _WIN32
    char sys[MAX_PATH]; GetSystemDirectoryA(sys, MAX_PATH);
    char cmd[2048]; snprintf(cmd, sizeof cmd, "\"%s\\tar.exe\" -xf \"%s\" -C \"%s\"", sys, arc, outdir);
    STARTUPINFOA si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 0;
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    int got = 0; char p[1024];
    for (int w = 0; w < NWANTED; w++) { snprintf(p, sizeof p, "%s/%s", outdir, wanted[w]); got += exists(p); }
    return got;
#else
    (void)arc; (void)outdir; return 0;
#endif
}

/* ---------------- ROM check screen ---------------- */
typedef struct { const char *name; uint32_t crc, size; } RomInfo;
static const RomInfo roms[] = GAME_ROMS;
#define NROMS ((int)(sizeof roms / sizeof roms[0]))
static int g_quit;

static uint32_t crc_tab[256];
static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crc_tab[i] = c; }
}

typedef struct { int state; uint32_t crc; Sint64 size; float progress; } RomRow;   /* state 0 pending 1 checking 2 good 3 bad crc 4 bad size 5 missing */

static void fill(SDL_Renderer *r, float x, float y, float w, float h, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 a)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, a); SDL_FRect f = { x, y, w, h }; SDL_RenderFillRect(r, &f);
}

static void draw_check(SDL_Renderer *r, int W, int H, const RomRow *row, int done, int bad, float t)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int y = 0; y < H; y += 4) {                                   /* deep blue-violet gradient */
        float k = (float)y / H;
        fill(r, 0, (float)y, (float)W, 4, (Uint8)(14 + 10 * k), (Uint8)(10 + 4 * k), (Uint8)(30 + 22 * (1 - k)), 255);
    }
    int px = H >= 900 ? 4 : H >= 700 ? 3 : 2, lh = 12 * px, cw = 6 * px;
    float x0 = W * 0.08f, y0 = H * 0.12f, tw = W * 0.84f;
    SDL_SetRenderDrawColor(r, 255, 200, 90, 255); hud_text(r, (int)x0, (int)y0, px + 1, "ROM CHECK");
    SDL_SetRenderDrawColor(r, 170, 170, 200, 255); hud_text(r, (int)x0, (int)(y0 + 13 * (px + 1)), px, GAME_TITLE " (" GAME_SET ")");
    float sweep = fmodf(t * 0.35f, 1.0f);                              /* animated accent line */
    fill(r, x0, y0 + 13 * (px + 1) + lh, tw, (float)px, 60, 50, 110, 255);
    fill(r, x0 + tw * sweep * 0.8f, y0 + 13 * (px + 1) + lh, tw * 0.2f, (float)px, 255, 200, 90, 255);
    float ty = y0 + 13 * (px + 1) + lh * 2.2f;
    int cx[5] = { 0, 15, 26, 36, 46 };
    static const char *hdr[5] = { "FILE", "SIZE", "CRC32", "EXPECTED", "STATUS" };
    SDL_SetRenderDrawColor(r, 140, 140, 175, 255);
    for (int c = 0; c < 5; c++) hud_text(r, (int)(x0 + cx[c] * cw), (int)ty, px, hdr[c]);
    for (int i = 0; i < NROMS; i++) {
        float y = ty + lh * (1.4f + 1.25f * i);
        fill(r, x0 - cw, y - px * 2.5f, tw + 2 * cw, lh, 255, 255, 255, i & 1 ? 10 : 22);
        char b[64];
        SDL_SetRenderDrawColor(r, 235, 235, 245, 255); hud_text(r, (int)x0, (int)y, px, roms[i].name);
        if (row[i].state >= 2 && row[i].state != 5) { snprintf(b, sizeof b, "%.1f MB", row[i].size / 1048576.0); hud_text(r, (int)(x0 + cx[1] * cw), (int)y, px, b); }
        if (row[i].state >= 2 && row[i].state != 5) { snprintf(b, sizeof b, "%08X", row[i].crc); hud_text(r, (int)(x0 + cx[2] * cw), (int)y, px, b); }
        SDL_SetRenderDrawColor(r, 150, 150, 170, 255);
        snprintf(b, sizeof b, "%08X", roms[i].crc); hud_text(r, (int)(x0 + cx[3] * cw), (int)y, px, b);
        float sx = x0 + cx[4] * cw;
        switch (row[i].state) {
        case 0: SDL_SetRenderDrawColor(r, 110, 110, 130, 255); hud_text(r, (int)sx, (int)y, px, "-"); break;
        case 1: fill(r, sx, y, 9 * cw, 7 * px, 60, 60, 90, 255); fill(r, sx, y, 9 * cw * row[i].progress, 7 * px, 255, 200, 90, 255); break;
        case 2: fill(r, sx - px, y - px, 6 * cw, 9 * px, 20, 120, 50, 255); SDL_SetRenderDrawColor(r, 120, 255, 140, 255); hud_text(r, (int)sx, (int)y, px, "GOOD"); break;
        default: fill(r, sx - px, y - px, 10 * cw, 9 * px, 130, 20, 30, 255); SDL_SetRenderDrawColor(r, 255, 110, 110, 255);
                 hud_text(r, (int)sx, (int)y, px, row[i].state == 3 ? "BAD CRC" : row[i].state == 4 ? "BAD SIZE" : "MISSING");
        }
    }
    float fy = ty + lh * (1.4f + 1.25f * NROMS) + lh;
    if (done) {
        if (!bad) { SDL_SetRenderDrawColor(r, 120, 255, 140, 255); hud_text(r, (int)x0, (int)fy, px, "ALL ROMS GOOD - PRESS ENTER OR A TO CONTINUE"); }
        else {
            SDL_SetRenderDrawColor(r, 255, 110, 110, 255); hud_text(r, (int)x0, (int)fy, px, "SOME ROMS DO NOT MATCH THE " GAME_SET " SET");
            SDL_SetRenderDrawColor(r, 220, 220, 235, 255); hud_text(r, (int)x0, (int)(fy + lh), px, "ENTER: CONTINUE ANYWAY    ESC: CHOOSE OTHER ROMS");
        }
    } else { SDL_SetRenderDrawColor(r, 170, 170, 200, 255); hud_text(r, (int)x0, (int)fy, px, "CHECKING..."); }
    SDL_RenderPresent(r);
}

/* 0 = Esc (choose again), 1 = continue, -1 = window closed (quit) */
static SDL_Gamepad *g_pads[8];
static int poll_keys(int done)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return -1;
        if (e.type == SDL_EVENT_GAMEPAD_ADDED)                         /* events only come from opened pads */
            for (int i = 0; i < 8; i++) if (!g_pads[i]) { g_pads[i] = SDL_OpenGamepad(e.gdevice.which); break; }
        if (!done) continue;
        if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE)) return 1;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) return 0;
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) return 1;
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) return 0;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) return 1;
    }
    return 2;
}

/* Shows the check screen for the ROM files in dir. 1 = go on with setup, 0 = choose again (or quit: g_quit) */
static int check_screen(const char *dir)
{
    SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    int W = dm ? dm->w * 6 / 10 : 1152, H = W * 9 / 16;
    SDL_Window *win = SDL_CreateWindow(GAME_TITLE " - ROM check", W, H, 0);
    SDL_Renderer *r = win ? SDL_CreateRenderer(win, NULL) : NULL;
    if (!r) { if (win) SDL_DestroyWindow(win); return 1; }                  /* no screen: just continue */
    SDL_SetRenderVSync(r, 1);
    SDL_GetCurrentRenderOutputSize(r, &W, &H);
    crc_init();
    RomRow row[NROMS]; memset(row, 0, sizeof row);
    int bad = 0, res = 2; Uint64 t0 = SDL_GetTicks();
    static uint8_t buf[1 << 20];
    for (int i = 0; i < NROMS && res == 2; i++) {
        char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, roms[i].name);
        SDL_IOStream *f = SDL_IOFromFile(p, "rb");
        if (!f) { row[i].state = 5; bad = 1; continue; }
        row[i].state = 1; row[i].size = SDL_GetIOSize(f);
        uint32_t c = 0xFFFFFFFFu; Sint64 doneb = 0; size_t n; Uint64 last = 0;
        while ((n = SDL_ReadIO(f, buf, sizeof buf)) > 0) {
            for (size_t k = 0; k < n; k++) c = crc_tab[(c ^ buf[k]) & 0xFF] ^ (c >> 8);
            doneb += (Sint64)n; row[i].progress = row[i].size > 0 ? (float)doneb / row[i].size : 1;
            if (SDL_GetTicks() - last > 30) { last = SDL_GetTicks(); res = poll_keys(0); if (res != 2) break; draw_check(r, W, H, row, 0, bad, (SDL_GetTicks() - t0) / 1000.f); }
        }
        SDL_CloseIO(f);
        row[i].crc = ~c;
        row[i].state = row[i].size != (Sint64)roms[i].size ? 4 : row[i].crc != roms[i].crc ? 3 : 2;
        if (row[i].state != 2) bad = 1;
        logf_("%s: crc %08x size %lld -> %s", roms[i].name, row[i].crc, (long long)row[i].size, row[i].state == 2 ? "good" : "bad");
    }
    while (res == 2) { res = poll_keys(1); draw_check(r, W, H, row, 1, bad, (SDL_GetTicks() - t0) / 1000.f); }
    SDL_DestroyRenderer(r); SDL_DestroyWindow(win);
    /* hand the pads back: close them and shut the gamepad subsystem down, so the game's own SDL_Init
       starts it again and gets SDL_EVENT_GAMEPAD_ADDED for every connected pad */
    for (int i = 0; i < 8; i++) if (g_pads[i]) { SDL_CloseGamepad(g_pads[i]); g_pads[i] = NULL; }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    if (res < 0) g_quit = 1;
    return res == 1;
}

static int assets_ready(void) { return exists("assets/program.bin") && exists("assets/nand.bin"); }

static int ends_with(const char *s, const char *e) { size_t a = strlen(s), b = strlen(e); return a >= b && !SDL_strcasecmp(s + a - b, e); }

/* archive -> TMPDIR -> setup; extracted copies are removed afterwards */
static int from_archive(const char *arc, int (*setup)(const char *))
{
    if (!exists(arc)) { logf_("%s: not found", arc); return 0; }
    MKDIR("assets"); MKDIR(TMPDIR);
    int got = ends_with(arc, ".7z") ? sevenz_extract(arc, TMPDIR) : zip_extract(arc, TMPDIR);
    int ok = got >= 2 && check_screen(TMPDIR) && setup(TMPDIR) == 0;
    logf_("%s: %d files, setup %s", arc, got, ok ? "ok" : "failed");
    char p[1024];
    for (int w = 0; w < NWANTED; w++) { snprintf(p, sizeof p, "%s/%s", TMPDIR, wanted[w]); remove(p); }
#ifdef _WIN32
    _rmdir(TMPDIR);
#else
    rmdir(TMPDIR);
#endif
    return ok;
}

static int from_path(const char *path, int (*setup)(const char *))
{
    if (ends_with(path, ".zip") || ends_with(path, ".7z")) return from_archive(path, setup);
    char p[1024]; snprintf(p, sizeof p, "%s/%s", path, GAME_U4);
    if (exists(p)) return check_screen(path) && setup(path) == 0;
    /* a file inside the ROM folder was picked: use its folder */
    snprintf(p, sizeof p, "%s", path);
    char *s = strrchr(p, '/'), *b = strrchr(p, '\\'); if (b > s) s = b;
    if (!s) return 0;
    *s = 0;
    char q[1024]; snprintf(q, sizeof q, "%s/%s", p, GAME_U4);
    return exists(q) && check_screen(p) && setup(p) == 0;
}

typedef struct { volatile int done; char path[1024]; } Pick;
static void SDLCALL picked(void *ud, const char *const *list, int filter)
{
    Pick *k = ud; (void)filter;
    if (list && list[0]) snprintf(k->path, sizeof k->path, "%s", list[0]);
    k->done = 1;
}

int rom_setup(int (*setup)(const char *romdir))
{
    static const char *dirs[] = { GAME_SET, "roms/" GAME_SET };
    static const char *arcs[] = { GAME_SET ".zip", "roms/" GAME_SET ".zip", GAME_SET ".7z", "roms/" GAME_SET ".7z" };
    char p[1024];
    for (int i = 0; i < 2 && !g_quit; i++) { snprintf(p, sizeof p, "%s/%s", dirs[i], GAME_U4); if (exists(p) && check_screen(dirs[i]) && setup(dirs[i]) == 0) return 0; }
    for (int i = 0; i < 4 && !g_quit; i++) if (from_archive(arcs[i], setup)) return 0;
    if (g_quit) return 1;
    if (assets_ready()) return 0;                 /* an import above already built assets/: never ask again */

    const char *msg = "The " GAME_SET " ROM set was not found.\n\nPut " GAME_SET ".zip, " GAME_SET ".7z or a folder named \""
                      GAME_SET "\" next to this program,\nor pick a ROM archive (Zip / 7z...) or folder (Folder...) now.\nIt is only needed once: the game keeps what it needs in assets\\.";
    for (;;) {
        const SDL_MessageBoxButtonData btn[] = {
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Zip / 7z..." },
            { 0, 2, "Folder..." },
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" } };
        SDL_MessageBoxData md = { SDL_MESSAGEBOX_INFORMATION, NULL, GAME_TITLE, msg, 3, btn, NULL };
        int which = 0;
        if (!SDL_ShowMessageBox(&md, &which) || which == 0) return 1;
        Pick k = { 0, "" };
        static const SDL_DialogFileFilter filt[] = { { "ROM set (zip, 7z)", "zip;7z" }, { "All files", "*" } };
        if (which == 1) SDL_ShowOpenFileDialog(picked, &k, NULL, filt, 2, NULL, false);
        else SDL_ShowOpenFolderDialog(picked, &k, NULL, NULL, false);
        while (!k.done) { SDL_Event e; while (SDL_PollEvent(&e)) {} SDL_Delay(10); }
        rom_chdir_base();                                               /* the dialog may change the working folder */
        if (!k.path[0]) continue;                                       /* cancelled */
        logf_("picked %s", k.path);
        if (from_path(k.path, setup) || assets_ready()) return 0;
        if (g_quit) return 1;
        msg = "That did not contain the " GAME_SET " ROMs (" GAME_U4 ", " GAME_U2 ").\n\n"
              "Choose " GAME_SET ".zip / .7z or the folder holding the ROM files.";
    }
}
