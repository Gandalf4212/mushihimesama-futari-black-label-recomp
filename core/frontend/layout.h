/* Minimal reader for MAME artwork layouts (.lay, the subset bezel packs use): <element name> with one
 * <image file>, and <view>s holding <screen>, <backdrop>, <overlay> and <bezel> items with <bounds>.
 * Drawing order (as MAME): backdrops, screen, overlays (multiplied over what is below), bezels (alpha).
 * Artwork files are the user's; nothing from a pack is part of this project. GPL-2.0-or-later. */
#ifndef LAYOUT_H
#define LAYOUT_H
#include <SDL3/SDL.h>

enum { LAY_BACKDROP, LAY_OVERLAY, LAY_BEZEL };
typedef struct { int elem, kind; SDL_FRect b; } LayItem;
typedef struct { char name[64]; SDL_FRect screen, box; int has_screen; LayItem item[16]; int n; } LayView;
typedef struct { char name[64], file[512]; SDL_Texture *tex; int tried; } LayElem;
typedef struct { LayElem elem[16]; int nelem; LayView view[8]; int nview; } Layout;

/* Parses path (a .lay); image files are relative to its folder and load on first use. 0 on failure. */
int  layout_load(Layout *l, const char *path);
void layout_free(Layout *l);
/* Texture for an element (NULL if its image is missing; a warning is printed once). */
SDL_Texture *layout_tex(Layout *l, SDL_Renderer *r, int elem);
#endif
