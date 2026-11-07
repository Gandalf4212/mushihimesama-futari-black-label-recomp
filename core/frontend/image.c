/* Image loading for artwork: PNG and BMP through SDL3; JPEG (and anything else Windows can decode)
 * through the Windows Imaging Component, so no third-party decoder is needed. GPL-2.0-or-later. */
#include "image.h"
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <wincodec.h>

static SDL_Surface *load_wic(const char *path)
{
    SDL_Surface *out = NULL;
    HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *f = NULL; IWICBitmapDecoder *d = NULL; IWICBitmapFrameDecode *fr = NULL; IWICFormatConverter *cv = NULL;
    wchar_t wp[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 1024)) goto done;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&f))) goto done;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(f, wp, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &d))) goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(d, 0, &fr))) goto done;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv))) goto done;
    if (FAILED(IWICFormatConverter_Initialize(cv, (IWICBitmapSource *)fr, &GUID_WICPixelFormat32bppBGRA,
                                              WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) goto done;
    UINT w = 0, h = 0;
    if (FAILED(IWICFormatConverter_GetSize(cv, &w, &h)) || !w || !h) goto done;
    out = SDL_CreateSurface((int)w, (int)h, SDL_PIXELFORMAT_BGRA32);
    if (out && FAILED(IWICFormatConverter_CopyPixels(cv, NULL, (UINT)out->pitch, (UINT)out->pitch * h, (BYTE *)out->pixels))) {
        SDL_DestroySurface(out); out = NULL;
    }
done:
    if (cv) IWICFormatConverter_Release(cv);
    if (fr) IWICBitmapFrameDecode_Release(fr);
    if (d) IWICBitmapDecoder_Release(d);
    if (f) IWICImagingFactory_Release(f);
    if (SUCCEEDED(co)) CoUninitialize();
    if (!out) SDL_SetError("cannot decode %s", path);
    return out;
}
#endif

SDL_Surface *image_load(const char *path)
{
    size_t n = strlen(path);
    const char *ext = n > 4 ? strrchr(path, '.') : NULL;
    if (ext && !SDL_strcasecmp(ext, ".png")) return SDL_LoadPNG(path);
    if (ext && !SDL_strcasecmp(ext, ".bmp")) return SDL_LoadBMP(path);
#ifdef _WIN32
    return load_wic(path);                         /* .jpg, .jpeg and other formats Windows knows */
#else
    SDL_SetError("%s: only PNG and BMP are supported on this platform", path);
    return NULL;
#endif
}
