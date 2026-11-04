# Status

**Last session:** Day 1 (2026-11-04). Milestones 1, 2, 3, 5, 6 done; milestone 7 first comparison done: rendering pixel-exact vs MAME (milestone 4 unit tests deferred).

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

## Next
1. Milestone 10 implementation per docs/timing.md. Files to read first (stage from
   F:\claude2\fbl\mame-mame0289\src\devices\cpu\sh\): `sh7709s.h` (cache constants), `sh_fe.cpp`
   (per-opcode base cycles), and grep sh3comn.cpp for WCR1/WCR2/MCR/BCR2 addresses. sh7709s.cpp is
   already summarized in docs/timing.md.
2. Gameplay comparison: input script with COIN1/START1/moves (MAME frame numbers), capture with
   `capture.bat 2600 3400 5 inputs.txt` in MAME, run ours with `--input inputs.txt --frame-offset 293`.
   Inputs land at different game states if loading drift differs; use attract-relative timing or
   start inputs after loading is complete (MAME frame >= 2000).
3. Milestone 8 (Vulkan renderer checked against blit_ref), 9 (YMZ770 audio), 10 (timing/cache model to
   remove the drift and reproduce slowdown).
