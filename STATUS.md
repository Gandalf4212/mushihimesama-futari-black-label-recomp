# Status

**Last session:** Day 1 (2026-11-04). Milestones 1, 2 and 3 done.

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

## Next (milestone 5 runtime; milestone 4 instruction tests can be folded in)
1. Instruction unit tests: random register/memory states, one instruction at a time, interpreter vs
   generated single-instruction functions (catches bugs lockstep can't, since both share isa.py bodies
   only the control-flow code differs; semantic bugs in isa.py need a second reference: compare a
   sample against MAME's SH-3 core or the SH-3 manual).
2. Runtime: SH7709S INTC (IRQ2 vsync at 60.0183806 Hz → INTEVT2 code for the vector table), TMU if used,
   ports/inputs, NAND K9F1G08U0M command set (read ID, read page), RTC9701 serial EEPROM, blitter register
   stubs. Read only the needed MAME files (`src/devices/cpu/sh/sh7709s*`, `sh3comn*`, `machine/nandflash*`,
   `machine/rtc9701*`) and check their license headers.
3. Goal: game runs past the idle loop, frame counter advances, NAND graphics load begins, blitter
   command lists appear in RAM (ready for milestone 6 reference blitter).
4. Fix the uncached-alias miss: dispatch should normalize alias for code addresses (already phys) -
   investigate why 0xAC002A54 isn't a block start (probably a mid-function entry via literal 0xAC002A54).
