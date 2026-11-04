# Status

**Last session:** Day 1 (2026-11-04). Milestones 1, 2, 3, 5 and 6 done (milestone 4 unit tests deferred).

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

## Next (milestone 7: frame checker vs MAME)
1. Input scripting in run_headless (`--input file`: frame-numbered port C/D/L states) to coin up/start.
2. Reference frames from MAME 0.289: ask the user to run in a MAME folder with the futaribl set:
   `mame futaribl -window -nothrottle -snapname %g/%i -snapshot_directory snaps` and press F12 at
   points, OR better: a small MAME Lua script (autoboot_script) that saves a snapshot every N frames
   with the same input script, then send me the PNGs (staged from F:\claude2\fbl). Snapshot = 320x240
   (check MAME's -snapsize / ROT270 rotation: use `-norotate` so it matches our raw buffer).
3. tools/frame_check.py: compare our PNGs vs MAME PNGs per frame, report first mismatching frame,
   pixel count and a diff image. Expect frame-number offsets (MAME frame 0 vs our boot): align on a
   distinctive frame.
4. Then milestone 8 (Vulkan renderer, verified against blit_ref) or 9 (audio), user's choice.
