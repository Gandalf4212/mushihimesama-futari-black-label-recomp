# How the recompiler works

1. **Boot** (`core/runtime/boot_runner.c`): the game's own startup code runs on the reference
   interpreter until `game.json boot.main_entry`. Result: the unpacked RAM image.
2. **Discovery** (`core/sh3/discover.py`): recursive descent from the main entry, the VBR handlers, every
   pointer in the exception/IRQ vector table, call targets, and every aligned word in the image that
   points into the code region (function-pointer tables; Cave's task system uses many). Candidates
   that are not reached by known control flow are accepted only if decoding never hits an illegal
   opcode. futaribl: 5,126 functions, ~1 MB of code.
3. **Generation** (`core/sh3/gen_c.py`): one C function per discovered function, instruction bodies
   taken from `isa.py` (the same text the interpreter is built from).
   - Entered through the dispatcher at any block start (`switch` on `s->pc`).
   - Calls, returns, indirect jumps: set `s->pc`, return to the dispatcher. The C stack never mirrors
     the guest stack, so task switches and PR tricks are safe. (Direct calls are a later optimization.)
   - Local branches are `goto`; backward branches check `s->cycles >= s->event_at` so interrupts and
     frame timing can be delivered inside long loops.
   - `s->cycles += N` per block (instruction count; the timing model will replace N with real costs).
4. **Dispatcher** (`core/runtime/dispatch.c`): direct table from address to function; unknown addresses
   run on the interpreter one instruction at a time and are logged as misses (feed them back to
   discovery with `--extra`).
5. **Lockstep test** (`core/runtime/lockstep_test.c`): boots, clones the machine, runs recompiled code
   and the interpreter to the same instruction count, compares all registers and all RAM.

Pipeline script: `tools/build_game.sh <set> <rom_dir>`. Everything it writes stays in `build/`.
