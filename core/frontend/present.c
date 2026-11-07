/* Presentation (see present.h). The shaders are ours (core/frontend/shaders/), compiled to SPIR-V
 * at build time into shaders_spv.h. SDL3's GPU renderer runs them as custom render states.
 * GPL-2.0-or-later. */
#include "present.h"
#include "image.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "shaders_spv.h"   /* generated: game_frag_spv[], bezel_frag_spv[] (+ _len) */

static SDL_GPURenderState *make_state(SDL_Renderer *ren, SDL_GPUDevice *dev, const unsigned char *code, size_t len)
{
    SDL_GPUShaderCreateInfo si; memset(&si, 0, sizeof si);
    si.code = code; si.code_size = len; si.entrypoint = "main";
    si.format = SDL_GPU_SHADERFORMAT_SPIRV; si.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    si.num_samplers = 1; si.num_uniform_buffers = 1;
    SDL_GPUShader *sh = SDL_CreateGPUShader(dev, &si);
    if (!sh) { fprintf(stderr, "shader: %s\n", SDL_GetError()); return NULL; }
    SDL_GPURenderStateCreateInfo ci; memset(&ci, 0, sizeof ci);
    ci.fragment_shader = sh;
    SDL_GPURenderState *st = SDL_CreateGPURenderState(ren, &ci);
    if (!st) fprintf(stderr, "render state: %s\n", SDL_GetError());
    return st;
}

int present_init(Presenter *p, SDL_Window *win, Config *c)
{
    memset(p, 0, sizeof *p);
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");                    /* our shaders are SPIR-V */
    p->ren = SDL_CreateRenderer(win, SDL_GPU_RENDERER);
    if (p->ren) {
        SDL_GPUDevice *dev = SDL_GetGPURendererDevice(p->ren);
        if (dev && (SDL_GetGPUShaderFormats(dev) & SDL_GPU_SHADERFORMAT_SPIRV)) {
            p->game = make_state(p->ren, dev, game_frag_spv, game_frag_spv_len);
            p->bezel = make_state(p->ren, dev, bezel_frag_spv, bezel_frag_spv_len);
        }
        p->gpu = p->game != NULL;
    } else {
        fprintf(stderr, "GPU renderer unavailable (%s): plain renderer, no shaders\n", SDL_GetError());
        p->ren = SDL_CreateRenderer(win, NULL);
    }
    if (!p->ren) return 0;
    p->tex = SDL_CreateTexture(p->ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
    /* the blitter leaves the top byte 0: XRGB and no blending (SDL3 blends ARGB textures, so 0 = invisible) */
    SDL_SetTextureBlendMode(p->tex, SDL_BLENDMODE_NONE);
    SDL_SetRenderVSync(p->ren, c->vsync && !c->uncapped ? 1 : 0);
    present_load_bezel(p, c);
    return p->tex != NULL;
}

void present_load_bezel(Presenter *p, Config *c)
{
    if (p->bezel_img) { SDL_DestroyTexture(p->bezel_img); p->bezel_img = NULL; }
    if (p->have_lay) { layout_free(&p->lay); p->have_lay = 0; }
    c->artwork_views = 0;
    const char *path = c->artwork;
    if (!path[0]) {                                                   /* default places for a bezel pack */
        static const char *auto_[] = { "Bezel/default.lay", "bezel/default.lay", "artwork/default.lay" };
        for (int i = 0; i < 3 && !path[0]; i++) { SDL_IOStream *f = SDL_IOFromFile(auto_[i], "rb"); if (f) { SDL_CloseIO(f); path = auto_[i]; } }
        if (!path[0]) return;
    }
    size_t n = strlen(path);
    if (n > 4 && !SDL_strcasecmp(path + n - 4, ".lay")) {
        p->have_lay = layout_load(&p->lay, path);
        if (p->have_lay) c->artwork_views = p->lay.nview;
        if (c->bezel - 1 > c->artwork_views) c->bezel = 1;
        return;
    }
    SDL_Surface *s = image_load(path);   /* PNG, BMP, JPEG (Windows) */
    if (!s) { fprintf(stderr, "artwork %s: %s\n", path, SDL_GetError()); return; }
    p->bezel_img = SDL_CreateTextureFromSurface(p->ren, s);
    SDL_DestroySurface(s);
    if (p->bezel_img) { SDL_SetTextureBlendMode(p->bezel_img, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(p->bezel_img, SDL_SCALEMODE_LINEAR); }
}

/* draws a layout item of one kind (MAME order: backdrops, screen, overlays multiplied, bezels alpha) */
static void lay_items(Presenter *p, const LayView *v, int kind, float k, float ox, float oy)
{
    for (int i = 0; i < v->n; i++) {
        if (v->item[i].kind != kind) continue;
        SDL_Texture *t = layout_tex(&p->lay, p->ren, v->item[i].elem); if (!t) continue;
        SDL_SetTextureBlendMode(t, kind == LAY_OVERLAY ? SDL_BLENDMODE_MOD : SDL_BLENDMODE_BLEND);
        const SDL_FRect *b = &v->item[i].b;
        SDL_FRect d = { ox + b->x * k, oy + b->y * k, b->w * k, b->h * k };
        SDL_RenderTexture(p->ren, t, NULL, &d);
    }
}

const char *present_backend(const Presenter *p) { return p->gpu ? "VULKAN" : "BASIC"; }

void present_draw(Presenter *p, const Config *c, Orientation o, const uint32_t *fb, int ow, int oh)
{
    SDL_Renderer *r = p->ren;
    if (fb) SDL_UpdateTexture(p->tex, NULL, fb, 320 * 4);
    int shader = p->gpu ? c->shader : SHADER_NONE;
    SDL_SetTextureScaleMode(p->tex, shader == SHADER_NONE ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);

    /* picture size on screen (vw x vh upright), integer or fit scaling, snapped to whole pixels */
    int yoko = o == ORIENT_YOKO;
    float vw = yoko ? 240.f : 320.f, vh = yoko ? 320.f : 240.f, s;
    if (c->scaling == SCALING_FIT) s = fminf(ow / vw, oh / vh);
    else { s = floorf(fminf(ow / vw, oh / vh)); if (s < 1) s = 1; }
    float dw = floorf(vw * s), dh = floorf(vh * s);
    float x = floorf((ow - dw) / 2), y = floorf((oh - dh) / 2);
    const LayView *lv = (yoko && p->have_lay && c->bezel >= 2 && c->bezel - 2 < p->lay.nview) ? &p->lay.view[c->bezel - 2] : NULL;
    float lk = 0, lx = 0, ly = 0;
    if (lv) {                                  /* artwork view: fit its box to the output, screen where it says */
        lk = fminf(ow / lv->box.w, oh / lv->box.h);
        lx = (ow - lv->box.w * lk) / 2 - lv->box.x * lk; ly = (oh - lv->box.h * lk) / 2 - lv->box.y * lk;
        x = floorf(lx + lv->screen.x * lk); y = floorf(ly + lv->screen.y * lk);
        dw = floorf(lv->screen.w * lk); dh = floorf(lv->screen.h * lk);
    }

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (lv) lay_items(p, lv, LAY_BACKDROP, lk, lx, ly);
    else if (p->gpu && p->bezel && c->bezel == 1 && yoko) {
        struct { float rect[4], outp[4]; } u = {
            { x / ow, y / oh, dw / ow, dh / oh }, { (float)ow, (float)oh, 1.0f, 0.30f } };
        SDL_SetGPURenderStateFragmentUniforms(p->bezel, 0, &u, sizeof u);
        SDL_SetGPURenderState(r, p->bezel);
        SDL_FRect full = { 0, 0, (float)ow, (float)oh };
        SDL_RenderTexture(r, p->tex, NULL, &full);
        SDL_SetGPURenderState(r, NULL);
    }
    /* the texture is 320x240 before rotation: its rect is (dh x dw) for yoko, centred, then rotated */
    SDL_FRect dst;
    if (yoko) { dst.w = dh; dst.h = dw; dst.x = x + (dw - dh) / 2; dst.y = y + (dh - dw) / 2; }
    else { dst.w = dw; dst.h = dh; dst.x = x; dst.y = y; }
    double angle = yoko ? 270.0 : o == ORIENT_TATE_CW ? 180.0 : 0.0;
    if (shader != SHADER_NONE) {
        static const float strength[4][3] = { {0, 0, 0}, {0, 0, 0}, {0.85f, 0.35f, 0}, {0.85f, 0.35f, 1.0f} };
        struct { float src[4], dst[4], opt[4]; } u = {
            { 320, 240, 1.f / 320, 1.f / 240 }, { dst.w, dst.h, yoko ? 1.f : 0.f, 0 },
            { (float)(shader - 1), strength[shader][0], strength[shader][1], strength[shader][2] } };
        SDL_SetGPURenderStateFragmentUniforms(p->game, 0, &u, sizeof u);
        SDL_SetGPURenderState(r, p->game);
    }
    SDL_RenderTextureRotated(r, p->tex, NULL, &dst, angle, NULL, SDL_FLIP_NONE);
    if (shader != SHADER_NONE) SDL_SetGPURenderState(r, NULL);
    if (lv) { lay_items(p, lv, LAY_OVERLAY, lk, lx, ly); lay_items(p, lv, LAY_BEZEL, lk, lx, ly); }
    else if (p->bezel_img && c->bezel >= 1 && yoko) {                  /* single PNG overlay, fit to the output */
        float iw, ih; SDL_GetTextureSize(p->bezel_img, &iw, &ih);
        float k = fminf(ow / iw, oh / ih);
        SDL_FRect b = { (ow - iw * k) / 2, (oh - ih * k) / 2, iw * k, ih * k };
        SDL_RenderTexture(r, p->bezel_img, NULL, &b);
    }
}
