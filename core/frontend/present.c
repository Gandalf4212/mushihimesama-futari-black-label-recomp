/* Presentation (see present.h). The shaders are ours (core/frontend/shaders/), compiled to SPIR-V
 * at build time into shaders_spv.h. SDL3's GPU renderer runs them as custom render states.
 * GPL-2.0-or-later. */
#include "present.h"
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

int present_init(Presenter *p, SDL_Window *win, const Config *c)
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

void present_load_bezel(Presenter *p, const Config *c)
{
    if (p->bezel_img) { SDL_DestroyTexture(p->bezel_img); p->bezel_img = NULL; }
    if (!c->bezel_image[0]) return;
    size_t n = strlen(c->bezel_image);
    int png = n > 4 && (!SDL_strcasecmp(c->bezel_image + n - 4, ".png"));
    SDL_Surface *s = png ? SDL_LoadPNG(c->bezel_image) : SDL_LoadBMP(c->bezel_image);
    if (!s) { fprintf(stderr, "bezel_image %s: %s\n", c->bezel_image, SDL_GetError()); return; }
    p->bezel_img = SDL_CreateTextureFromSurface(p->ren, s);
    SDL_DestroySurface(s);
    if (p->bezel_img) { SDL_SetTextureBlendMode(p->bezel_img, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(p->bezel_img, SDL_SCALEMODE_LINEAR); }
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

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (p->gpu && p->bezel && c->bezel && yoko) {
        struct { float rect[4], outp[4]; } u = {
            { x / ow, y / oh, dw / ow, dh / oh }, { (float)ow, (float)oh, 1.0f, 0.55f } };
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
    if (p->bezel_img && c->bezel) {                                  /* user overlay, fit to the output */
        float iw, ih; SDL_GetTextureSize(p->bezel_img, &iw, &ih);
        float k = fminf(ow / iw, oh / ih);
        SDL_FRect b = { (ow - iw * k) / 2, (oh - ih * k) / 2, iw * k, ih * k };
        SDL_RenderTexture(r, p->bezel_img, NULL, &b);
    }
}
