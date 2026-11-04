# Plan

## Goals (in priority order)
1. **Accuracy**: frame-exact against MAME, pixel-exact blitter, original timing and slowdown.
2. **Very low frame times and latency**: late input polling, no queued frames, VRR, optional run-ahead.
3. **Display**: **Yoko by default**; Tate (CW/CCW) in settings; controls rotate with the screen;
   integer scaling; optional CRT/scanline shaders.
4. **Options**: original slowdown **on by default**, with a switch to turn it off.
5. **Reusable for every CV1000 game**: the core knows nothing about Futari.

## Copyright rules
- The repo holds only original work: tools, recompiler, runtime, renderer, audio, docs, ROM checksums.
- Users supply their ROM set. Setup verifies it, extracts `assets/`, runs the boot code to build the RAM
  image, generates C code, and compiles. After that the ROM is no longer needed.
- Generated C, RAM images and assets are never committed (`.gitignore` + `tools/check_no_game_data.py`).
- MAME knowledge is credited; check each MAME file's license before porting its code.

## Layout
```
core/                shared by all CV1000 games (later split into its own repo, e.g. cv1k-recomp)
  sh3/               SH-3 decoder + reference interpreter (oracle) + C code generator
  runtime/           memory map, on-chip peripherals (INTC, TMU, ports), IRQs, NAND, RTC/EEPROM, timing model
  render/            CPU reference blitter, Vulkan blitter, presentation (yoko/tate, scaling, shaders)
  audio/             YMZ770
  frontend/          window, input mapping, settings, run-ahead
games/<set>/         romset.json (generated from MAME) + game.json (entry points, quirks, hooks)
tools/               ROM verify, disassembler, extractor, frame checker, safety check
docs/                hardware notes, adding a game
```

## Milestones
| # | Milestone | Done when |
|---|---|---|
| 1 | Repo, ROM verify, hardware notes, boot analysis | ✅ Day 1 |
| 2 | SH-3 decoder + reference interpreter; run boot to main entry; dump RAM image | RAM image produced; interpreter matches capstone decode on all reachable code |
| 3 | Code discovery (function/branch targets from RAM image) + C generator | Program translates and compiles |
| 4 | Instruction tests: generated C vs interpreter | All instruction tests pass |
| 5 | Runtime: memory map, INTC/TMU, vsync IRQ2, inputs, NAND, RTC | Main loop runs, waits on the idle variable |
| 6 | CPU reference blitter | Attract-mode frames match MAME |
| 7 | Frame checker vs MAME (scripted inputs, first-diff report, long unattended runs) | Runs headless in CI |
| 8 | Vulkan blitter | Matches the reference renderer frame for frame |
| 9 | YMZ770 audio | Music and SFX play |
| 10 | Timing model for slowdown + no-slowdown option | Slowdown matches MAME/real footage |
| 11 | Frontend: yoko/tate, input, latency work, run-ahead, Windows build, release workflow | Playable on PC |

## Ideas borrowed (from darkuni/burgertime-arcade-landscape-recomp, ideas only, no code)
- Frame-by-frame oracle against a reference, run over very long input scripts.
- GitHub release workflow building Windows/Linux/macOS.
- ROM search order: next to the executable, `roms/`, working dir, or a path argument.
