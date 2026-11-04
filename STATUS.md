# Status

**Last session:** Day 1 (2026-11-04). Milestones 1, 2, 3 and 5 done (milestone 4 unit tests deferred).

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

## Next (milestone 6: CPU reference blitter)
1. Read MAME `src/mame/cave/cv1k_v.cpp/.h` (BSD-3, already staged at /mnt/user-data/uploads/fbl/mame-mame0289/
   src/mame/cave/): command list format (gfx_exec / gfx_create_shadow_copy), VRAM layout (8192x4096?
   RGB555+transparency), blit modes (flip, tint, alpha, src/dst blend modes), clip, scroll, firmware
   version differences (FW_A..D chosen by bitstream checksum: compute the checksum in fw upload).
2. `core/render/blit_ref.c`: exact CPU implementation, hooked via `Blitter.on_exec`; frame output =
   320x240 window of VRAM at scroll. Write PNG frames from run_headless (`--png-every N`).
3. Get reference frames from MAME for comparison (user can run MAME `futaribl` with
   `-snapshot_directory`, or I build MAME's cv1k driver only, if cheap) → milestone 7 frame checker.
4. Inputs: coin/start scripting in run_headless (`--input script`) to get past attract into gameplay.
Deferred: milestone 4 instruction unit tests (random states, interpreter vs single-instruction
generated code); IRQ replay lockstep (cv1k irq_log is in place).
