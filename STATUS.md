# Status

**Last session:** Day 1 (2026-11-04). Milestones 1 and 2 done.

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

## Next (milestone 3: code discovery + C generator)
1. `core/sh3/discover.py`: walk code from entry points (main entry, VBR+0x100/0x400/0x600 handlers,
   bsr/jsr targets, jump tables like the IRQ dispatcher at 0x8C000600) over `build/ram.bin`; collect
   functions and basic blocks; report indirect jumps (`jmp/jsr @Rn`, `braf/bsrf`) that need tables.
2. `core/sh3/gen_c.py`: emit one C function per discovered function, reusing isa.py bodies; delay slots
   inlined; indirect jumps through a dispatch table (address → function), with interpreter fallback for
   any address not in the table (log it, so discovery can be improved).
3. Test: generated code vs interpreter on the same boot/main-loop run (register + RAM compare).
4. Then milestone 5 runtime: INTC/TMU, IRQ2 vsync, inputs, NAND reads, RTC9701.
Read only `src/devices/cpu/sh/` on-chip peripheral files when the runtime needs them (check license).

## Open questions
- Timing/slowdown model (milestone 10).
- Whether any code is copied/modified at runtime after boot (self-modifying or overlays): discovery
  should check by comparing RAM code regions later in a run.
