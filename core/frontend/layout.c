/* MAME .lay subset reader (see layout.h). GPL-2.0-or-later. */
#include "layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int attr(const char *tag, const char *end, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    for (const char *p = tag; p && p < end; p++) {
        p = strstr(p, key); if (!p || p >= end) return 0;
        if ((p == tag || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') && p[kl] == '=' && (p[kl + 1] == '"' || p[kl + 1] == '\'')) {
            char q = p[kl + 1]; const char *v = p + kl + 2, *e = strchr(v, q);
            if (!e || e > end) return 0;
            size_t len = (size_t)(e - v) < n - 1 ? (size_t)(e - v) : n - 1;
            memcpy(out, v, len); out[len] = 0; return 1;
        }
    }
    return 0;
}

static float fattr(const char *tag, const char *end, const char *key, float def)
{
    char v[32]; return attr(tag, end, key, v, sizeof v) ? (float)atof(v) : def;
}

/* <bounds .../> inside [s, e): x y width height (or left top right bottom) */
static int bounds(const char *s, const char *e, SDL_FRect *b)
{
    const char *t = strstr(s, "<bounds"); if (!t || t >= e) return 0;
    const char *te = strchr(t, '>'); if (!te) return 0;
    char tmp[8];
    if (attr(t, te, "left", tmp, sizeof tmp)) {
        float l = fattr(t, te, "left", 0), tp = fattr(t, te, "top", 0);
        b->x = l; b->y = tp; b->w = fattr(t, te, "right", 1) - l; b->h = fattr(t, te, "bottom", 1) - tp;
    } else { b->x = fattr(t, te, "x", 0); b->y = fattr(t, te, "y", 0); b->w = fattr(t, te, "width", 1); b->h = fattr(t, te, "height", 1); }
    return 1;
}

static void grow(SDL_FRect *box, int *init, const SDL_FRect *b)
{
    if (!*init) { *box = *b; *init = 1; return; }
    float x0 = SDL_min(box->x, b->x), y0 = SDL_min(box->y, b->y);
    float x1 = SDL_max(box->x + box->w, b->x + b->w), y1 = SDL_max(box->y + box->h, b->y + b->h);
    box->x = x0; box->y = y0; box->w = x1 - x0; box->h = y1 - y0;
}

int layout_load(Layout *l, const char *path)
{
    memset(l, 0, sizeof *l);
    size_t sz; char *x = (char *)SDL_LoadFile(path, &sz);
    if (!x) { fprintf(stderr, "layout %s: %s\n", path, SDL_GetError()); return 0; }
    char dir[512]; snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/'), *bs = strrchr(dir, '\\'); if (bs > slash) slash = bs;
    if (slash) slash[1] = 0; else dir[0] = 0;
    for (char *c = strstr(x, "<!--"); c; c = strstr(c, "<!--")) {          /* blank out comments */
        char *e = strstr(c, "-->"); if (!e) { *c = 0; break; }
        memset(c, ' ', (size_t)(e + 3 - c));
    }
    /* elements */
    for (char *p = strstr(x, "<element "); p && l->nelem < 16; p = strstr(p + 1, "<element ")) {
        char *te = strchr(p, '>'), *end = strstr(p, "</element>"); if (!te) break;
        LayElem *el = &l->elem[l->nelem];
        if (!attr(p, te, "name", el->name, sizeof el->name)) continue;       /* <element ref=...> in a view */
        char *img = strstr(p, "<image"); char file[256];
        if (!img || (end && img > end)) continue;
        char *ie = strchr(img, '>'); if (!ie || !attr(img, ie, "file", file, sizeof file)) continue;
        snprintf(el->file, sizeof el->file, "%s%s", dir, file);
        l->nelem++;
    }
    /* views */
    for (char *v = strstr(x, "<view "); v && l->nview < 8; v = strstr(v + 1, "<view ")) {
        char *ve = strstr(v, "</view>"); if (!ve) break;
        LayView *w = &l->view[l->nview]; int init = 0;
        attr(v, strchr(v, '>'), "name", w->name, sizeof w->name);
        static const char *tags[] = { "<backdrop", "<overlay", "<bezel", "<screen" };
        /* walk the items in document order */
        for (char *p = v + 5; p < ve; ) {
            int best = -1; char *bp = ve;
            for (int k = 0; k < 4; k++) { char *q = strstr(p, tags[k]); if (q && q < bp) { bp = q; best = k; } }
            if (best < 0) break;
            char *te = strchr(bp, '>'); char *close = strstr(bp, best == 3 ? "</screen>" : best == 0 ? "</backdrop>" : best == 1 ? "</overlay>" : "</bezel>");
            char *ie = (close && close < ve) ? close : te;
            SDL_FRect b;
            if (bounds(bp, ie + 1, &b)) {
                if (best == 3) { if (!w->has_screen) { w->screen = b; w->has_screen = 1; } }
                else if (w->n < 16) {
                    char en[64]; int ei = -1;
                    if (attr(bp, te, "element", en, sizeof en)) for (int k = 0; k < l->nelem; k++) if (!strcmp(l->elem[k].name, en)) ei = k;
                    if (ei >= 0) { w->item[w->n].elem = ei; w->item[w->n].kind = best; w->item[w->n].b = b; w->n++; }
                }
                grow(&w->box, &init, &b);
            }
            p = ie + 1;
        }
        if (w->has_screen) l->nview++;
    }
    SDL_free(x);
    if (!l->nview) fprintf(stderr, "layout %s: no usable view\n", path);
    return l->nview > 0;
}

SDL_Texture *layout_tex(Layout *l, SDL_Renderer *r, int elem)
{
    LayElem *e = &l->elem[elem];
    if (e->tex || e->tried) return e->tex;
    e->tried = 1;
    SDL_Surface *s = SDL_LoadPNG(e->file);
    if (!s) { fprintf(stderr, "artwork %s: %s\n", e->file, SDL_GetError()); return NULL; }
    e->tex = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    if (e->tex) SDL_SetTextureScaleMode(e->tex, SDL_SCALEMODE_LINEAR);
    return e->tex;
}

void layout_free(Layout *l)
{
    for (int i = 0; i < l->nelem; i++) if (l->elem[i].tex) SDL_DestroyTexture(l->elem[i].tex);
    memset(l, 0, sizeof *l);
}
