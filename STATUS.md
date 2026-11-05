# Status

**Last session:** Day 1 (2026-11-04). Milestones 1, 2, 3, 5, 6 done; milestone 7 rendering pixel-exact vs MAME; milestone 10 timing model in (MAME 0.289 parity) (milestone 4 unit tests deferred).

## Done
- ROM set verified against MAME master (futaribl block identical to 0.289): all 4 SHA1s OK.
- `games/*/romset.json` for all 31 CV1000 sets (`tools/mame_romsets.py`).
- Tools: `verify_roms.py`, `shdis.py` (capstone), `check_no_game_data.py` + `.githooks/pre-commit`.
- Hardware notes and boot sequence: `docs/hardware/cv1000.md`.
- **core/sh3**: `isa.py` is the single instruction table (160 SH-3 instructions, decode + C semantics).
  `gen_interp.py` generates the interpreter (`build/sh3_interp.inc`); `sh3_core.c` has the step loop,
  delay slots, register banks, div1, mac.w/mac.l, interrupt entry.
  Decode checked against capstone over all 65536 opcodes: identical on SH-3; the only differences are
  452 SH-2A/SH-4A-only opcodes capstone accepts in SH-3 mode (movua, clips, divu, ...).
- **core/runtime/boot_runner.c**: runs the game's own boot code on the interpreter until the main entry,
  writes the RAM image. futaribl: reaches `0x0C062FA0` after 37,994,413 instructions; only device access
  during boot is one write to blitter `0xB8000010`. Image checked: idle loop is exactly at `0x0C05176A`
  (matches MAME's speedup), IRQ vector at `0x8C000600` dispatches through a jump table.
- Build: `make` → `build/boot_runner`. Run:
  `./build/boot_runner <futaribli_u4> 800000 0C062FA0 build/ram.bin`
  (`build/` is gitignored; ram.bin is game data, never commit it).

- **Milestone 3 (recompiler)**, see `docs/recompiler.md`:
  `core/sh3/discover.py` (5,126 functions, 87,869 blocks, ~1 MB code for futaribl; pointer scan of the
  whole image finds task/function-pointer tables), `core/sh3/gen_c.py` (98 MB of C in 35 files, compiles
  in ~1.5 min on 2 cores at -O1), `core/runtime/dispatch.c` (direct table + interpreter fallback with
  miss log), `core/runtime/lockstep_test.c`.
  **Result:** 400M instructions after boot, recompiled == interpreter (all registers + all 8 MB RAM),
  ending at the idle loop 0x0C05176A (waiting for vsync IRQ2, not implemented yet). 400M instructions
  ≈ 4 s of real hardware time; test takes ~3 s including the interpreter run.
  Only misses: cache routine run from uncached alias `0xAC002A54…` during startup (harmless).
- One command: `tools/build_game.sh futaribl <rom_dir>` (verify, boot, discover, generate, compile,
  lockstep). Outputs in `build/futaribl/` (gitignored).

- **Milestone 5 (runtime)**: `core/runtime/cv1k.c/.h` = memory map, INTC (IRQ2 vsync at 60.0183806 Hz,
  1,706,145 instruction-cycles per frame for now), TRAPA exception, DMAC (4 ch, immediate transfers through
  the bus), ports/inputs, NAND K9F1G08U0M (read/ID/status/program/erase), RTC9701 serial EEPROM+RTC,
  blitter registers (logged, `on_exec` hook for the renderer), FPGA bitstream counter. Hardware facts in
  `docs/hardware/cv1000.md` ("On-chip peripherals").
  `core/runtime/run_headless.c`: boots and runs N frames, recompiled or `--interp`; `--misses file` writes
  addresses that needed the interpreter; `tools/build_game.sh` feeds them back into discovery
  (profile-guided). **futaribl now: 0 interpreter fallbacks**, 5,624 functions.
  **Result (1200 frames):** game boots, uploads FPGA bitstream, streams 9,256 KB from NAND by DMA,
  then renders attract mode at exactly 1 blitter list per frame. Recompiled 618 fps headless vs
  interpreter 69 fps; both give identical IRQ/DMA/blit counts (1172 / 4702 / 1137).

- **Milestone 6 (CPU reference blitter)**: `core/render/blit_ref.c/.h` follows MAME cv1k_v.cpp +
  cv1k_v_in.ipp + cv1k_v_pixel.ipp (BSD-3) as one readable routine: command list ops 0x2000 upload,
  0x1000 draw, 0xC000 clip, 0x0000/0xF000 end; VRAM 8192x4096 ARGB1555 (bit15 = opaque); 5-bit
  channels with colrtable/colrtable_rev/colrtable_add lookup math; 8 source x 8 dest blend modes, tint
  (0x20 = neutral, byte>>2), flip x/y, transparency, clip (screen clip = clip reg ±32 or full VRAM).
  Reproduces MAME's add_with_clr_square quirk (green/blue use clr0.r) to stay bit-exact with MAME; flag
  it for a real-hardware check later. Output: 320x240 window at scroll, channels <<3 (as MAME).
  `core/render/png.c`: dependency-free PNG writer. `run_headless --png <dir> <every N>` saves frames.
  **Result:** 1800 frames: 971k draws, 11.6k uploads, 0 bad ops. Frame 600 = boot self-test screen
  ("MUSHIHIMESAMA FUTARI BL. 2009.11.27 / CHECKING MEMORY / CODE AREA 00710250 OK!"); frame 1800 =
  attract "Today's Original Ranking" screen, visually correct (raw unrotated buffer, game is ROT270).
  Longer runs find new code: rerun tools/build_game.sh to compile recorded misses.

- **Milestone 7 tools**: `run_headless --input script [--frame-offset K]` (script lines
  `start_frame duration TOKEN...`, tokens COIN1 START1 UP DOWN LEFT RIGHT B1-B4, P2_*, SERVICE, TEST;
  script frames are MAME frame numbers, ours = MAME - K), `--png-range from to`.
  `tools/mame/capture.lua` + `capture.bat`: MAME autoboot script applying the same script and saving
  `snaps/futaribl/f_NNNNN.png` (-norotate = raw buffer). `tools/frame_check.py ours mame [--diff x.png]`
  finds the offset by exact frame hashes, counts identical aligned frames, reports first mismatch.
  Self-test (our frames renumbered +25) passes: offset 25 found, 61/61 identical.
  Input test: COIN1 at 1900, START1 at 1960 reaches the Character Select screen.

- **Milestone 7 first result (2026-11-04)**: MAME 0.289 capture of attract frames 1500-2100 (no inputs,
  -norotate; user's copy at F:\claude2\fbl\mame_snaps_attract, f_NNNNN.png). Sampled 26 frames (1500,
  1600, 1700, 1800, 1900, 2000-2100 every 5). **Every MAME frame in our range has an exact pixel match**
  in our output (1001 frames rendered, ours 1300-2300). Offset MAME - ours = 290 for MAME 1600-1900 and
  293 for 2000-2100: our instruction-count clock finishes a loading phase 3 frames sooner. That is CPU
  timing (milestone 10: cycle costs + SH-3 cache model), not rendering.
  Note: MAME warns "imperfect timing" at startup: press a key, or set `skip_warnings 1` in ui.ini.
  frame_check.py assumes one global offset, so it reports the 290-offset frames as mismatches;
  add a per-frame best-match mode (ad hoc check is in the session log: hash lookup per MAME frame).

- `frame_check.py --per-frame` added (drift curve; confirms single +3 drift before MAME frame 2000).
- Decision (user): **target MAME timing for now**, fine-tune against the real PCB later.
  Design written: `docs/timing.md` (port of MAME sh7709s.cpp cache/memory penalty model; hooks in bus
  callbacks + per-instruction fetch in generated code; `--timing exact|fast|off`).

- **Milestone 10 (timing, MAME 0.289 parity)**: `core/runtime/timing.c/.h` = port of sh7709s.cpp model:
  16 KB 4-way LRU cache (16 B lines, regions 0xA…/0xE… uncached), access_penalty (burst SDRAM, wait
  states from BCR2/WCR1/WCR2/MCR at 0xFFFFFF62/64/66/68, area switch, SDRAM page precharge, write-back
  buffer), cache address array flush (0xF0000000), CCR clear (0xFFFFFFEC bit 3). MAME quirks kept:
  fetch penalty only for opcode groups 0/4/F; accesses >= 0xE0000000 re-charge the previous data
  address; base cycles from sh_fe.cpp (bt/bf 3, bra/bsr/rts/braf/bt-s/bf-s 2, rte 4, trapa 8, mac 3,
  mul 2, tas 4, ldc.l sr 3, stc.l sr 2, sleep 3). DMA accesses are not charged. Note: MAME master adds
  device-access charges (update_access_cycles) that 0.289 does not have - we follow 0.289.
  Hooks: SH3.fetch_hook (per executed instruction, interpreter and generated code via SH3_FETCH),
  bus.fetch16 (opcode fetch without data timing), bus accessors charge data penalties.
  `run_headless --timing off` = old instruction-count clock. Boot now runs inside the frame clock
  (frames counted from power-on like MAME): main entry reached at frame 100.
  **Result:** all 26 sampled MAME frames (1500-2100) identical at a CONSTANT offset (MAME = ours - 68):
  the +3 loading drift is gone. Remaining: 68-frame constant lag accumulated before MAME frame 1500
  (boot / FPGA upload / memory check). Need early MAME frames to locate it (capture 0-1500 every 10).
  Cost: build ~4 min (hooks double the C), ~100-200 fps headless.

- **Timing parity with MAME 0.289 DRC (2026-11-04 evening)**: two MAME recompiler behaviors matter as much
  as the cache model, both now reproduced:
  1. PC-relative literal loads (`mov.w/mov.l @(disp,PC)`) are collapsed to immediates (cv1k speedups set
     SH2DRC_FASTEST_OPTIONS, i.e. no STRICT_PCREL): no memory access, no penalty, no cache touch.
     isa.py uses RW_PCREL/RL_PCREL (untimed fetch path).
  2. Base cycles are accumulated and only charged at MAME's update points (`core/sh3/sh3_cycles.h`):
     flush after every mov memory access and the sts.l/stc.l/lds.l/ldc.l stack forms + tas.b (not in a
     delay slot); charge at unconditional control flow (incl. slot) and when falling into a branch target;
     conditional branches charge if taken and DROP the pending count if not taken (MAME resets
     compiler.cycles at compile time after the taken path's update). SH3.acct/acc; generated code
     ACC/FLUSH/DROP macros; interpreter mirrors it (without branch-target flushes).
  **Result:** power-on to attract tracks MAME within ±3 frames: MAME 211 +3, 401 0, 801-1001 -1,
  1441+ -1; attract 1500-2100: 26/26 frames identical at constant offset -1.
  EEPROM check screens (MAME 1011-1431) differ only because the user's MAME had saved nvram (CHECKING
  vs our INITIALIZE); user has deleted MAME's nvram\futaribl - recapture to confirm.
  capture.lua input tokens fixed (P1_/P2_ prefixes via token_to_input_type's player).
  `run_headless --tmstats a b`: cycle breakdown (penalties, misses, uncached accesses) for frames a..b.

- **Fresh-nvram recapture** (F:\claude2\fbl\mame_snaps_attract-3, frames 1..2091 every 10, MAME nvram
  deleted first): all 16 sampled frames 211-2091 match exactly, incl. EEPROM INITIALIZE screens.
  Offsets (MAME - ours): +3 at 211-221, 0 at 401, -1 from 801 to 2091 (constant).

- **Milestone 11 first playable build (frontend)**: `core/frontend/main_sdl.c` (SDL2): Yoko default (game
  rotated 270 deg CW, integer-scaled in a 16:9 window), `--tate-cw` (180) / `--tate-ccw` (0), `--scale`,
  `--fullscreen`/F11, `--no-slowdown` (timing off), `--rotate-controls`; keyboard + SDL gamepad; inputs
  read right before each frame; frame loop identical to run_headless (IRQ check order matters: a
  `sleep` must be woken right after vsync). First run auto-creates `assets/` (program.bin, nand.bin)
  from a `futaribl/` or `roms/futaribl/` folder next to the exe; ROM not needed afterwards.
  Saves on exit: NAND settings block (game keeps settings/high scores in NAND, not the RTC EEPROM) and
  EEPROM if written. `tools/build_frontend.sh <set> linux|windows` (Windows via mingw-w64 +
  SDL2-devel-2.30.9-mingw from GitHub releases, unpacked at /home/claude/deps). Linux build verified
  headless (SDL offscreen): Yoko upright. Windows exe built, untested (no Wine here) -> user tests.
  Known limits: no sound, software blitter, vsync-paced at monitor rate (game is 60.018 Hz).
- `games/futaribl/inputs/stage1.txt`: coin/start/defaults/play stage 1 (MAME frames). Ours reaches
  Character Select ~2600 and stage 1 gameplay 3000-4200 with offset -1.

- **Frontend input + config (2026-11-04)**: `core/frontend/input.c` (P1/P2 keyboard with MAME default keys
  + Z/X/C/V alternates, 2 gamepads, coin 5/6, start 1/2, service 9, test F2; free play default: a Start
  press inserts a coin (4 frames) then presses Start; `--coins` for coin-op behavior), `core/frontend/
  config.c` (config.toml subset: [video] orientation/scale/fullscreen, [game] slowdown/freeplay,
  [input] rotate_controls; defaults written on first run; CLI flags + `--set section.key=value` +
  `--config file` override). Free play is a frontend feature for now; a later option is setting the
  game's own FREE PLAY in its NAND settings.

## Decisions (2026-11-04)
- **No Vulkan blitter.** The CPU reference blitter (MAME-exact) renders the game image; the GPU is used
  only for presentation. Milestone 8 is now: **GPU presentation layer on SDL3's GPU API** (Vulkan /
  D3D12 / Metal backends) with our own shaders: CRT (scanlines, mask, glow), curved CRT, bezel images with
  a configurable screen rect, integer/fit scaling, low-latency presentation (1 frame in flight, VRR /
  tearing option). Optional later: librashader for RetroArch presets.
- Overlay (requested): "FPS / CPU % / GPU %" + frame-time graph. CPU % = emulation time / 16.66 ms;
  GPU % needs GPU timing (SDL3 GPU may not expose timestamps: fall back to measured submit/present time).
- F1 settings menu (orientation, scale, shader/bezel, slowdown, free play, key/pad mapping), writes
  config.toml.

## Next
1. **Sound (milestone 9)**: YMZ770 (MAME src/devices/sound/ymz770.cpp + its MPEG-like decoder
   mpeg_audio.cpp; check licenses), sound ROMs u23/u24 (word-swapped) into assets/ at setup, SDL audio
   output; headless WAV dump for checking against MAME (`mame -wavwrite`).
2. GPU presentation layer (SDL3 GPU) + CRT/curved shaders + bezels.
3. F1 menu + overlay (needs a small bitmap font: write our own 8x8 font, no third-party font files).
4. Stage-1 gameplay comparison when mame_snaps_stage1 exists.
0. Stage-1 gameplay comparison: user runs (nvram deleted) `capture.bat 2400 4200 10 stage1.txt`,
   copies to F:\claude2\fbl\mame_snaps_stage1; ours: run_headless ... 4300 --input
   games/futaribl/inputs/stage1.txt --frame-offset -1 --png dir 1 --png-range 2350 4300; frame_check.
   Then user feedback on the Windows build (does it start, speed, frame pacing, controls).
1. Optional polish: the remaining 1-3 frame differences (first screen +3, then -1): suspects = interpreter lacks branch-target
   flushes during boot, MAME DRC block/sequence boundaries (max sequence length, page splits), IRQ
   acceptance points (MAME checks at update points), cache initial state.
2. Gameplay capture with inputs (now that capture.lua inputs work), blitter busy-time model.
3. Milestone 8 (Vulkan) / 9 (audio) / 11 (frontend).
