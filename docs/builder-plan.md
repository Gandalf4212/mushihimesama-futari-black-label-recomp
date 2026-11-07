# Builder plan (v0.8.0): ship only our code, compile the game on the player's PC

## Why
`futaribl.exe` today contains the game's SH-3 program translated to C and compiled (build/<set>/gen).
That is Cave's code in another form, so the public release must not contain it. Instead the release
ships our runtime plus an open-source toolchain, and builds the game code from the player's own ROMs
on first run (decision 2026-11-06, user: "option 2").

## Shape of the release
```
futaribl.exe        launcher + runtime + frontend (ours, no game code)
SDL3.dll
toolchain/          portable C compiler (w64devkit: mingw-w64 GCC, public-domain kit, GPL GCC)
recomp/             our translator (discover + gen_c) and runtime headers for the generated code
hints/futaribl.txt  extra code addresses (today's misses.txt: addresses only, no game bytes)
README.txt, LICENSE.txt
```
After the first run: `assets/` (as now) plus `assets/game.dll` (the compiled game code, local only).

## Runtime split
- Generated code goes into `game.dll`, built locally. It exports its dispatch table (address -> function)
  and an ABI version + the u4 CRC it was built from. No imports beyond the C runtime.
- `futaribl.exe` loads `assets/game.dll` with LoadLibrary, checks ABI and CRC, registers the table with
  dispatch.c. Without a valid DLL it can still run everything on the interpreter (slow fallback, and a
  "build needed" message).
- Today dispatch.c links the generated table statically: needs an indirection (pointer to table set at
  load time). Generated functions call runtime helpers (memory access, sh3 state): either pass a
  function table into the DLL at init, or link the DLL against an import lib of the exe. Prefer the
  callback table: no import lib, simpler ABI check.

## First-run flow (after the ROM check screen)
1. boot_runner logic (already C, move into the exe): run the boot code -> ram image in memory/temp.
2. discover + gen_c: produce C into `assets/_build/` (~100 MB). Either embed Python (embeddable zip,
   ~10 MB, PSF licence) or port discover.py / gen_c.py to C. Decide after measuring their size; a C port
   removes a dependency and is faster.
3. Compile the ~43 files with the bundled gcc -O1, in parallel (one per core), link `game.dll`.
   2 cores: ~10 min; 8 cores: ~2-3 min. Progress screen in the ROM-check style (file n / 43, elapsed).
4. Delete `assets/_build/`. Launch.
The second build pass is replaced by the shipped hints file (addresses found by running the game).

## Open questions (next session)
- Size of discover.py / gen_c.py (C port vs embedded Python).
- w64devkit size after trimming (gcc, ld, as, headers only) and its licence notices to ship.
- Exact runtime-helper ABI for generated code (what gen_c emits calls to).
- Rebuild trigger: new release with a changed translator -> ABI/version bump -> rebuild prompt.
- Linux later: same flow with the system compiler.
