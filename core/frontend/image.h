/* Artwork image loading (PNG, BMP; JPEG on Windows). GPL-2.0-or-later. */
#ifndef IMAGE_H
#define IMAGE_H
#include <SDL3/SDL.h>
SDL_Surface *image_load(const char *path);   /* NULL on failure (SDL_GetError) */
#endif
