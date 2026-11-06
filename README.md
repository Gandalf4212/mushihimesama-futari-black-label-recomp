# Mushihime-Sama Futari Black Label — Static Recompilation

A work-in-progress native PC port of the CV1000 arcade game, built by statically recompiling its SH-3 program code and running it on a native runtime with a Vulkan renderer.

**This repository contains no game code or assets.** You must supply your own `futaribl` ROM set. A setup step verifies it, extracts assets to `assets/`, generates the recompiled code locally, and builds the game. After setup the ROM is no longer needed.

The core (`core/`) is written to serve every Cave CV1000 game; per-game data lives in `games/<set>/`.
See `PLAN.md` for goals and milestones, `STATUS.md` for current progress, and `docs/hardware/cv1000.md`
for hardware notes.

Verify a ROM set: `python3 tools/verify_roms.py games/futaribl/romset.json path/to/futaribl(.zip)`

Contributors: run `git config core.hooksPath .githooks` once so commits are checked for game data.

Hardware knowledge credits: the MAME project (`src/mame/cave/cv1k.cpp` and related devices).
`core/sound/mpeg_audio.*` (Olivier Galibert) and `core/sound/ymz770.cpp` (port of the YMZ770 device by
Olivier Galibert, R. Belmont, MetalliC) come from MAME 0.289 and remain under MAME's BSD-3-Clause
licence (headers kept); the blitter busy-time and DMA/timer/cache timing models follow MAME's
`cv1k_v.cpp`, `sh4dmac.cpp`, `sh4tmu.cpp`, `sh7709s.cpp` and `sh4.cpp` (BSD-3-Clause).

License: GPL-2.0-or-later.
