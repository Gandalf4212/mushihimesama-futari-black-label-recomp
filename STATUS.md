# Status

**Last session:** Day 1 (2026-11-04). Milestone 1 done.

## Done
- ROM set verified against MAME master (futaribl block identical to 0.289): all 4 SHA1s OK.
- `games/*/romset.json` generated for all 31 CV1000 sets (`tools/mame_romsets.py`).
- Tools: `verify_roms.py`, `shdis.py` (capstone-based), `check_no_game_data.py` + `.githooks/pre-commit`.
- Boot sequence decoded, see `docs/hardware/cv1000.md`. Main program is packed in ROM and unpacked to
  RAM by the game's own code; plan is to run the boot on our interpreter and recompile the RAM image.
- NAND layout understood: raw page reads, game parses its own block table.

## Next (milestone 2)
1. `core/sh3/`: SH-3 instruction table (decode + semantics), written once and shared by the interpreter
   and C generator. Check decode against capstone over the whole ROM.
2. Minimal runtime for boot: RAM/ROM map, on-chip register stubs (writes via GBR to `0xFFFFFFxx`,
   `0xA4000xxx` ports).
3. Run from `0xA0000000` to `0x0C062FA0`; dump RAM image to `generated/` (gitignored).
4. Read only: MAME `src/devices/cpu/sh/` files needed for semantics (check license headers).

## Open questions
- Unpacker header at ROM `0x51000` (`002C85B8 001382FF …`): not needed if the interpreter runs it.
- Exact cycle costs for the slowdown model (milestone 10).
