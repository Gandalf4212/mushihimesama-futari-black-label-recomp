# Mushihime-Sama Futari Black Label — native PC static recompilation

A native Windows port of Cave's CV1000 arcade game **Mushihime-Sama Futari Black Label** (MAME set
`futaribl`), made by **statically recompiling** the game's SH-3 program into C and running it on a
native model of the CV1000 board. It is not an emulator running the ROM's code instruction by
instruction: the game logic runs as compiled x86-64 code, with the board's timing modelled closely
enough that speed and slowdown match MAME frame for frame.

**Status: playable on Windows (v0.8.0).** The release contains none of Cave's code
or data: on first start it builds the game code from *your own* ROM set, on your PC.

> This project contains no game code, graphics, sound or other data from the game. You need your own
> `futaribl` ROM set (MAME 0.289 naming). Mushihime-Sama Futari is © Cave. This project is not
> affiliated with or endorsed by Cave.

## Features

**The game**
- Full game, recompiled: 6,592 functions (≈1 MB of SH-3 code) translated to C; the interpreter only
  covers the rare code paths found nowhere else (logged, then added as hints).
- Accurate speed and slowdown: SH7709S cache/bus timing, the CV1000 blitter's busy time and MAME's
  scheduler quirks are modelled, so slowdown matches MAME 0.289 side by side. Frames 502–700 of a
  test run are frame-exact against MAME; in a stage 1 run 73 % of frames flip on the same vblank
  and the rest within ±1. Original slowdown is on by default and can be switched off.
- Native rate of 60.024 Hz, or one game frame per display refresh on ~60 Hz monitors.
- YMZ770 sound (music and effects), saved settings and high scores (EEPROM).

**Display**
- SDL3 with a Vulkan presenter: Yoko (horizontal) by default, Tate clockwise / counter-clockwise.
- Fit or integer scaling, window scale, fullscreen, VSync on/off, uncapped benchmark mode.
- Our own shaders: none, sharp-bilinear, CRT (scanlines + aperture grille) and curved CRT.
- Our own Yoko bezel, or your own MAME `.lay` artwork packs (PNG and JPEG).
- Performance HUD (F10): game / present / cap times.

**Controls**
- Keyboard (MAME defaults) and up to two gamepads; remap every key and pad input, including sticks
  and triggers, in F1 > CONTROLS.
- Free play (on by default): Start inserts a credit when needed; coins mode available.
- Optional per-player rumble when a ship is lost.

**Setup**
- First-run ROM import from a folder, a `.zip`, a `.7z` or a file picker, with a CRC check screen.
- First-run build: the program is translated and compiled into `assets\game.dll` with the bundled
  compiler (a trimmed w64devkit/GCC), with a progress screen. About 3 minutes on 8 cores.

## Playing

1. Download the release zip and unpack it anywhere.
2. Put your `futaribl` ROM set next to `futaribl.exe` (`futaribl.zip`, `futaribl.7z` or a `futaribl`
   folder), or pick it when asked.
3. Start `futaribl.exe`. The ROM check runs, then the one-time build, then the game.

Requirements: 64-bit Windows 10/11, a GPU with Vulkan drivers, about 2 GB of free RAM per CPU core
used during the one-time build, and about 350 MB of disk space after setup. See `README.txt` in the
release for keys, settings and command-line options.

## How it works

1. **Boot**: the game's own startup code runs on a reference SH-3 interpreter until the main entry;
   the result is the unpacked program in RAM.
2. **Discovery** (`core/sh3/recomp.c`): recursive descent from the entry points, vector tables, call
   targets and code pointers found in the image, plus a list of hint addresses.
3. **Generation**: one C function per game function; instruction semantics come from one table
   (`core/sh3/isa.py`) shared with the interpreter, with cycle accounting and interrupt/update points
   placed where MAME's recompiler has them.
4. **Build**: the C is compiled into `game.dll`, which the runtime loads through a small versioned ABI
   (`core/runtime/recomp_abi.h`).
5. **Runtime**: SH7709S peripherals (interrupt controller, timers, DMA, cache timing), the CV1000
   blitter, NAND, RTC/EEPROM and the YMZ770 sound chip, driven by an SDL3 frontend.

More: `docs/recompiler.md`, `docs/timing.md`, `docs/timing-sync-notes.md`, `docs/hardware/cv1000.md`,
`docs/builder-plan.md`.

## Building from source

The release is cross-built on Linux (Ubuntu 24.04 tested) for Windows. You need the ROM set to generate
and test the game code locally (never commit it).

```sh
git config core.hooksPath .githooks                  # commits are checked for game data
apt-get install -y mingw-w64 zip lua5.4 glslang-tools p7zip-full
pip install capstone --break-system-packages         # disassembler for the tools
# SDL3-devel-3.4.20-mingw.tar.gz (libsdl-org/SDL releases) unpacked in ../deps
tools/build_game.sh futaribl <romdir>                # boot, translate, compile, headless test run
tools/build_frontend.sh futaribl windows             # futaribl.exe + game.dll (for local testing)
# w64devkit-x64-2.9.1.7z.exe (skeeto/w64devkit releases) unpacked, then:
tools/trim_toolchain.sh <w64devkit dir> ../deps/toolchain <GPLv3 COPYING3>
tools/package_release.sh futaribl futaribl-v0.8.0-windows-x64.zip
```

Checks: `tools/check_translator.sh futaribl` (C translator = Python reference, byte for byte),
`build/futaribl/run_headless` with `--input` scripts and `--watch-log` for comparisons against MAME
(Lua capture scripts in `tools/mame/`).

## Repository layout

```
core/sh3/        SH-3 instruction table, reference interpreter, translator (C + Python reference)
core/runtime/    CV1000 machine model, timing, dispatcher, game.dll ABI, headless runner
core/render/     reference blitter, PNG writer
core/sound/      YMZ770 + MPEG audio (from MAME, BSD-3-Clause)
core/frontend/   SDL3 frontend: presenter, shaders, input, menu, ROM import, first-run builder
games/futaribl/  romset.json (checksums), game.json (entry points, RAM addresses), hints, test inputs
tools/           build, packaging, ROM verify, MAME comparison tools
docs/            hardware and design notes
```

The core is written to serve other CV1000 games later; per-game data lives in `games/<set>/`
(checksums for the whole CV1000 library are already there).

## Copyright rules for contributors

- The repository holds only original work: code, tools, docs, ROM checksums and addresses.
- Never commit ROMs, generated C, RAM images, `assets/`, executables or artwork. `.gitignore` and the
  pre-commit hook (`tools/check_no_game_data.py`) enforce this.
- Release zips contain only our code, the bundled toolchain and its licences.

## Credits and licences

- This project: GPL-2.0-or-later (`LICENSE`).
- Hardware knowledge: the MAME project (`src/mame/cave/cv1k.cpp` and related devices).
  `core/sound/mpeg_audio.*` (Olivier Galibert) and `core/sound/ymz770.*` (port of the YMZ770 device by
  Olivier Galibert, R. Belmont, MetalliC) come from MAME 0.289 and keep MAME's BSD-3-Clause licence
  (headers kept). The timing models follow MAME's `cv1k_v.cpp`, `sh7709s.cpp`, `sh4.cpp`,
  `sh4dmac.cpp` and `sh4tmu.cpp` (BSD-3-Clause), independently implemented.
- Bundled compiler: [w64devkit](https://github.com/skeeto/w64devkit) 2.9.1 (public domain) with GCC
  and binutils (GPLv3) and the Mingw-w64 runtime (see `toolchain/` in the release for licences and
  sources).
- [SDL3](https://libsdl.org) (zlib licence).
- Ideas (no code) from darkuni/burgertime-arcade-landscape-recomp: frame-by-frame oracle testing and
  the ROM search order.
