/* First-run ROM discovery (folder, zip, 7z, or a file/folder picker). GPL-2.0-or-later. */
#ifndef ROMFIND_H
#define ROMFIND_H
/* Calls setup(romdir) on the ROM set it finds (extracting archives to a temporary folder first).
 * Returns 0 once setup succeeded, 1 if the user quit. Needs SDL_Init(SDL_INIT_VIDEO) for the dialogs. */
int rom_setup(int (*setup)(const char *romdir));
#endif
