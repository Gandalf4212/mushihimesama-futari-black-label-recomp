# Changelog

## v0.8.0 (2026-11-07) — first public release

The release contains none of the game's code: on first start the game code is built from the player's
own ROM set into `assets\game.dll` with the bundled compiler.

- First-run builder: boot, translate (new C translator, byte-identical to the Python reference and
  10x faster), parallel compile with a trimmed w64devkit 2.9.1 (GCC 16.2.0), link; progress screen;
  log in `assets\build.log`. Rebuilds by itself when a new version changes the game-code ABI.
- Game code loaded from `game.dll` through a versioned ABI, with an interpreter fallback.
- Free play: one Start press starts the game; no stray credits (credit-aware coin insert).
- ROM import: no "ROM set not found" dialog after a successful import.
- Version shown in the window title and the F1 menu.

## Before v0.8.0 (private test builds)

- SH-3 recompiler, CV1000 runtime, reference blitter, YMZ770 sound.
- Timing matched to MAME 0.289 (cache/bus timing, DRC fetch quirks, idle speedup, vblank timing,
  interrupt update points): slowdown indistinguishable from MAME.
- SDL3 frontend: Vulkan presenter, shaders (sharp / CRT / curved CRT), Yoko bezel, MAME .lay artwork
  (PNG/JPEG), fit / integer scaling, benchmark HUD, ROM import (folder / zip / 7z / picker) with CRC
  check, key + pad remapping, rumble.
