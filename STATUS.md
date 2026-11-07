# Status

## START HERE (handoff 2026-11-07, evening)

### v0.8.0 release prepared (2026-11-07)
* test27 user-confirmed (first-run build works on Windows). Repo cleaned for release: new README.md
  (features, how it works, building), CHANGELOG.md, docs/release.md (checklist + GPL toolchain sources),
  PLAN.md removed (folded into README), stray .pyc untracked, version 0.8.0 (core/frontend/version.h:
  window title + F1 menu).
* Copyright audit of all 69 commits / 403 blobs: largest blob 54 KB (STATUS.md); no ROM data, generated
  C, RAM images, disassembly listings or artwork ever committed; only binaries were .pyc of our scripts.
  Third-party code: MAME BSD-3 sound files (headers kept); everything else ours.
* Release files (outputs/release): futaribl-v0.8.0-windows-x64.zip (no game.dll), w64devkit-2.9.1-source.zip.
  Release CI: .github/workflows/release.yml builds + packages on publish (no ROM; game.dll guard) and
  attaches the zip + toolchain sources (GPL). Simulated locally from a clean clone: same 56-file zip.
  **Left: user publishes release v0.8.0 on GitHub, then tests the attached zip.**


### Where we are
* Latest test build: **F:\claude2\fbl\futaribl_windows_test27.zip** (no game code: the first public-safe build;: it contains the recompiled game
  code, see "Builder"). User-confirmed working on Windows: SDL3 frontend, Vulkan shaders (none / sharp /
  crt / crt-curved), our Yoko bezel + MAME .lay artwork packs, fit / integer scaling, uncapped benchmark
  HUD, first-run ROM import (folder / zip / 7z / picker) with the CRC check screen, key + pad remapping
  (F1 > CONTROLS), rumble per player (P1 0x0C383E4F, P2 0x0C383F1B lives bytes).
* Packaging: `tools/package_release.sh futaribl out.zip` -> exe, SDL3.dll, README.txt (ours,
  games/futaribl/README.txt), LICENSE.txt. Never ROMs, artwork, generated C.
* Decisions: 60.024 Hz is the native rate ("pcb" option removed); free play stays the coin trick (the board
  has no free-play setting); rotate controls stays manual; rumble off by default; artwork packs are the
  user's (LaunchBox "Cave Shoot Part 1": copy default.lay, vert_screen_bezel.png, vert_screen_mask.png,
  04.png -> art1.png into Bezel\); SDL3 GPU = Vulkan only.

* **test24: user-confirmed 2026-11-07** (ROM import no longer shows the "not found" dialog; the guard
  stays, root cause never found).
* **test25 (2026-11-07): free play fixed (to confirm).** User on test24: Start in attract added a coin but
  didn't start (fire doesn't start either: Cave games start on Start only); a 2nd Start added another
  coin and started; Start in game kept adding coins. Measured with run_headless input scripts (coin at
  2200): the credit byte (**0x0C0601E7**, game.json "credits", found by RAM diff) counts at +5 frames,
  and a Start less than 10 frames after the coin is ignored; the old trick pressed Start at +8. Now
  (input.c): Start with 0 credits -> coin 4 frames, wait for credit > 0, +8 frames, Start 6 frames;
  credit already in -> Start passes through; our coin still unused 60 frames after our Start (Start while
  playing) -> credit byte written back to 0. Lives bytes can't tell the attract demo from play (P1 lives
  = 3 in the demo too). To confirm: one Start starts from attract, P2 join and continue still work,
  in-game Start leaves CREDIT 0.

* **test25: user-confirmed 2026-11-07** (free play: one Start starts; no stray credits).
* **test26 (2026-11-07): builder step (a) done, to confirm.** The Windows exe has no game code any more
  (584 KB, was 124 MB): the generated code is in game.dll, loaded from assets/game.dll, else game.dll next
  to the exe (test builds ship it there). ABI: core/runtime/recomp_abi.h (RECOMP_ABI 1; host callback
  table for the 6 helpers the generated C calls: sh3_miss, sh3_miss_at, sh3_bad_slot, sh3_set_sr,
  sh3_div1, sh3_macl; DLL info = abi, sizeof(SH3), u4 CRC, table). core/runtime/recomp_dll.c is linked
  into the DLL (forwarders + exported recomp_dll_init); generated C unchanged. core/frontend/gamedll.c:
  LoadLibrary + checks; on failure a warning box and the interpreter. dispatch_init(ram, table, count).
  Checked: Windows run_headless (-DUSE_GAMEDLL, under wine) through game.dll vs Linux static, stage1
  inputs, 3000 frames: per-frame watch log and PNGs at 1000/2000/3000 identical. Packaging:
  `tools/package_release.sh <set> out.zip --with-game-code` adds game.dll (private test builds only);
  without the flag the zip has none of Cave's code. build_frontend.sh skips up-to-date gen objects
  (relink ~20 s). **User-confirmed 2026-11-07** (plays as test25; ROM import works).
* **Builder steps (b) + (c) done 2026-11-07 (no user test needed: build-side only).** core/sh3/recomp.c (+ .h):
  C port of discover.py + gen_c.py, same work order and literal heuristics; instruction table from isa.py
  via core/sh3/gen_isa_c.py -> build/isa_table.inc (make build/recomp). Output byte-identical to Python
  with and without hints (45 / 35 files), 0.6 s vs 5.8 s. `tools/check_translator.sh <set>` re-checks it
  (run after any isa.py / gen_c.py / recomp.c change). Hints: games/futaribl/hints.txt (1462 runtime-miss
  addresses, addresses only); build_game.sh now uses build/recomp with hints.txt + build misses.txt.
  Python discover/gen_c stay as the reference.
* **Builder steps (d) + (e) (2026-11-07, test27, to confirm).** core/frontend/builder.c: when game.dll is
  missing/outdated and toolchain/ + recomp/ are next to the exe, main_sdl runs game_build(): boot
  (boot_run_image in boot_runner.c, 0.4 s) -> recomp_translate into assets/_build -> parallel gcc
  (CreateProcess, CREATE_NO_WINDOW, below-normal priority; jobs = min(cores, (RAM MB-2048)/1800), the biggest
  file needs ~1.8 GB in cc1) -> link assets/game.dll -> delete _build. Progress screen in the ROM-check style
  (headless if no window). Log: assets/build.log (kept _build on failure). Release layout: exe, SDL3.dll,
  recomp/ (sh3.h, sh3_ops.h, recomp_abi.h, recomp_dll.c, hints.txt), toolchain/ (trimmed w64devkit 2.9.1 =
  GCC 16.2.0: tools/trim_toolchain.sh <unpacked kit> ../deps/toolchain <COPYING3>; 59 MB, 24 MB zipped;
  whitelist from gcc -M / ld -t; gcc found via -Btoolchain/bin/). Licences in toolchain/: COPYING3.txt (GPLv3,
  GCC/binutils), COPYING.MinGW-w64-runtime.txt, README.txt (sources). **Release to-do: publish the GCC,
  binutils, Mingw-w64 source archives for w64devkit 2.9.1 with the GitHub release (GPLv3 source offer).**
  Kit download: github.com/skeeto/w64devkit/releases v2.9.1 w64devkit-x64-2.9.1.7z.exe (7z x).
  Checked under wine (2 cores, 8 GB): full build 17.5 min, game.dll loads (77080 entries), _build removed;
  run_headless through that game.dll = Linux static build (stage1, 3000 frames). test27 = public layout
  (package_release.sh without --with-game-code), 26 MB. **To confirm on Windows: fresh folder + ROMs ->
  ROM check -> building screen -> game starts; build time; 2nd start skips the build.**
  Ideas: smaller gen files (per-file < 150) to cut cc1 peak memory; -O1 vs -O2 timing.

### Next, in order (target v0.8.0)
1. **Builder (the v0.8.0 blocker)**: the public release must not contain Cave's code. Plan in
   docs/builder-plan.md: futaribl.exe = launcher + runtime + frontend (ours); game code compiled on the
   player's PC into assets/game.dll by a bundled portable GCC (w64devkit), after the ROM check screen.
   Steps: (a) dispatch.c: generated table behind a pointer + a callback table for the runtime helpers the
   generated code calls, ABI version + u4 CRC check, LoadLibrary of game.dll, interpreter fallback;
   (b) port core/sh3/discover.py + gen_c.py (+ isa.py tables; 634 lines total) to C inside the exe,
   or as a small tool exe; check output is byte-identical to the Python one; (c) ship misses.txt as
   hints/<set>.txt (addresses only) instead of the second build pass; (d) trim w64devkit, ship its
   licence notices; (e) first-run "building" screen (ROM-check style: file n/43, elapsed) and a rebuild
   prompt when the ABI version changes. Expect ~2-3 min compile on 8 cores, ~10 on 2.
2. Reset game from the F1 menu (needs a cv1k teardown + re-init).
3. Compare sound against MAME -wavwrite (same stage1.txt inputs, nvram deleted) vs
   `run_headless ... --sound u23 u24 --wav`.
4. Milestone 4: instruction unit tests.
5. Linux build: needs SDL3 (not packaged on Ubuntu 24.04: build from source); JPEG artwork is Windows-only (WIC).
6. Optional sync leftovers (below).

### Frame pacing / sync vs MAME 0.289: what was done, what is left
Full detail: docs/timing-sync-notes.md (sections 4, 6b, 6c). Summary:
* Mechanism: game speed and slowdown come from how many SH-3 cycles each game frame costs vs. VBLANK, so
  we model MAME's sh7709s cache/memory timing (docs/timing.md) and its scheduler quirks.
* MAME-parity fixes in: (1) instruction fetches charged at MAME's **stale DRC PC** (SH3.fpc; MAME only
  refreshes it at timeslice ends, exceptions/IRQ/TRAPA, RTE, LDC SR checkints, the pcflush address and
  the nocode handler; `--real-fetch` = old model); (2) MAME's **idle speedup** (10 us / 1024-cycle spin at
  0x0C05176A, ram var 0x0C002310; game.json "idle"; frontend calls cv1k_set_idle); (3) **VBLANK at
  (k+1)/60.024 s** (measured with flaglog); (4) **IRQ acceptance and timeslice ends at MAME's update
  points** (gen_c.py UPDATE_POINT after flushing memory ops, event checks on forward/backward branches
  and fall-ins, none on not-taken branches), serviced in place via SH3.service.
* Results: frames 502-700 frame-exact vs MAME with equal 5 us polling on both sides; unperturbed stage1:
  73 % of flips on the same VBLANK, the rest +-1 VBLANK (work ending right at a VBLANK); a fixed 2-frame
  shift at power-on from the first ~29 boot frames (no effect on speed). User: slowdown side by side with
  MAME indistinguishable, sound clean, no stutter. Heavy frames 6.3 ms host time on the 2-core box.
* Frontend pacing: wall clock at 60.024 Hz (frame_rate native), or "display" = one frame per refresh
  (MAME -syncrefresh) on 59-61 Hz displays; vsync separate; uncapped = present flat out, game paced.
* Left (optional): (a) where MAME refreshes its stale fetch PC during boot (markers MAME 5/18/28/29/45 vs
  ours 6/20/29/31/47; --real-fetch 6/19/29/30/46: other scheduler events or the nocode handler on first
  execution of a block?) -> fixes the 2-frame power-on shift; (b) mirror update points in the
  interpreter (it can exit after every instruction); (c) heavy frames ~0.02 ms (0.06 %) fast.
* References on F:\claude2\fbl: watch.txt, watch_boot.txt, watch_stage1.txt (per-frame watchlog: use
  these), mame_flags_500.txt (5 us flaglog: compare only with `--slice-us 5`). Delete
  F:\mame0.289\nvram\futaribl before every MAME capture; watchlog.bat address lists use "_".

### Working notes
* Workspace: clone, `git config core.hooksPath .githooks`; stage the 4 ROMs from F:\claude2\fbl\futaribl;
  `pip install capstone --break-system-packages`; `apt-get install -y mingw-w64 zip lua5.4 glslang-tools`;
  SDL3-devel-3.4.20-mingw.tar.gz from github.com/libsdl-org/SDL releases into /home/claude/deps (the
  GitHub API is blocked; `git ls-remote --tags` lists versions).
* Build: `tools/build_game.sh futaribl <romdir>` twice (~6.5 min each), then
  `tools/build_frontend.sh futaribl windows` (~10 min; compiles the shaders too). For frontend-only
  changes recompile the changed core/frontend/*.c into build/futaribl/windows/obj and relink (see the
  g++ line in build_frontend.sh), ~10 s.
* **run_headless RAM size is 800000** (8 MB): `run_headless u4 u2 800000 0C062FA0 <frames> --idle-pc 0C05176A`.
  With 8000000 the blitter reads garbage (black PNGs) and --dump-ram writes 128 MB per frame.
  `--watch-log file a,b,c` takes comma-separated addresses. RAM dumps: stream them (8 MB each), don't
  stack hundreds in memory.
* Deliver files: SendUserFile, then device_commit_files to F:\claude2\fbl with a new file name each time.
* User budget is tight: small slices, filtered output, no subagents; end each session with STATUS,
  commit, push, handoff.

## Session log 2026-11-06 (details, newest first)
* Milestones: 1, 2, 3, 5, 6, 10 done; 7 (pixel-exact rendering, timing close to MAME) ongoing;
  9 (sound) first cut done; 11 (frontend) playable on Windows with HUD (F10) and sound.
* Latest Windows build: test9 (see below). Earlier: futaribl_windows_test7.zip (2026-11-06). test6 crackled and slowed
  down in heavy scenes (user): host overruns from (a) a gen pass without the recorded misses and (b) a
  linear search in dispatch.c note_miss once its 4096-entry table was full; fixed (hash, 2nd build pass,
  in-place event service SH3.service). Gameplay host time on the 2-core build box: 12.1 -> 6.3 ms mean.
  Always run tools/build_game.sh twice before a release build. **User on test7: sound clean, no stutter,
  slowdown side by side with MAME "super close" (maybe a smidge more, can't tell).** test6 also had: wall-clock pacing at
  60.024 Hz (config game.frame_rate = mame|pcb, --pcb-rate), video.vsync / --no-vsync, audio.volume /
  --volume, idle speedup wired in, all 2026-11-06 timing fixes. To confirm: smooth on the user's
  display, no audio crackle, volume works. (test5: gamepad-duplication fix, confirmed.) User report on test4: sound great, HUD fine.
* **Bug fixed in test5 (to confirm):** after a death, Start brought in P1 and P2, both on one pad.
  Cause: core/frontend/input.c opened the same controller twice (explicit open at startup + SDL's
  CONTROLLERDEVICEADDED for already-connected pads), so one pad drove both players' inputs incl.
  Start, and free play coined + started both. Now an already-open pad is ignored.
* **v0.8.0 plan (user, 2026-11-06):** remaining hurdles = (1) control remapping [done in test18] and
  (2) the **builder**: rc1's exe contains the recompiled game code (Cave's program in translated form),
  so it must not be released publicly. Ship our runtime + toolchain and compile the game on the player's
  PC from their ROMs. Plan: docs/builder-plan.md. rc1 stays a private test build.
* **test19 (2026-11-06):** test18's PAD rows (bottom of the list) were not found by the user and action rows
  ignored pads. Now each action row takes a key or a pad button (button rows; pad mapping shared by both
  pads, clashes swap), Del clears keys, X clears the pad button; menu box 56 chars wide.
* **test20 (2026-11-06):** pad bindings per player action (directions too), two each, buttons or stick /
  trigger directions (code: 0, 1+button, PAD_AXIS+2*axis+neg; config pad_up = "dpup, -lefty"). Capture
  takes a key, pad button or stick push (>24000); one pad input drives one action (taken from others).
* **test21 (2026-11-06):** RESET TO DEFAULTS moved to the top of the CONTROLS page (user didn't find it at the bottom).
* **test22 (2026-11-06): rumble.** Lives bytes found by RAM diff of run_headless dumps and confirmed against
  screenshots (HUD ship icons): **P1 0x0C383E4F, P2 0x0C383F1B** (lives incl. the ship in play, start 3,
  player structs 0xCC apart). In game.json "lives" -> GAME_LIVES_P1/P2 -> main_sdl.c rumbles that
  player's pad (SDL_RumbleGamepad) when its byte drops by 1. Config [input] rumble_p1 / rumble_p2 = 0/1/2
  (default 0 = off, per user), F1 > RUMBLE P1 / P2, --rumble / --no-rumble. User: works first try.
  Rotate controls stays manual (decided: picture is upright in all modes when the monitor is turned). Earlier 0x0C200DB0 candidate was wrong (bad RAM size, see below).
* **run_headless RAM size is 800000 (8 MB, game.json ram_size), not 8000000.** With 8000000 the blitter
  reads its lists from the wrong place: --png frames are black and --dump-ram writes 128 MB per frame.
  (The handoff text had 8000000.)
* **test18 (2026-11-06): control remapping.** config.h bindings: key[ACT_COUNT][2] (P1/P2 up..coin, service,
  test; SDL scancode names in config.toml [input], e.g. p1_b1 = "Left Ctrl, Z") and pad[PAD_COUNT]
  (B1-B4, start, coin for both pads; SDL gamepad button names; directions fixed d-pad + left stick).
  F1 > CONTROLS... page (menu.c controls_event/draw): Enter/A capture, Del/X clear, RESET TO DEFAULTS,
  Esc/B back; F1/F10/F11 cannot be bound. input.c reads the bindings. Untested on Windows.
* **Release candidate 1 (2026-11-06): F:\claude2\fbl\futaribl_windows_rc1.zip.** Clean tools/build_frontend.sh run
  (SDL3, shaders, all sources) OK. Packaged by tools/package_release.sh: exe, SDL3.dll, README.txt (ours,
  games/futaribl/README.txt: controls, settings, ROM import, bezel packs incl. the LaunchBox "Cave Shoot
  Part 1" pack: default.lay, vert_screen_bezel.png, vert_screen_mask.png, 04.png -> art1.png), LICENSE.
  Debug --shot/--shot-menu removed from the frontend; core tool hooks (tool_event, dispatch_report, miss
  log) kept for run_headless / sync work. To confirm: pads after a ROM import.
* **Free play decision (2026-11-06, user):** the CV1000 board has no free-play setting, so the frontend's coin
  trick (input.c) stays. The "free play from the game's own setting" item is dropped.
* **test16 (2026-11-06):** check screen now opens gamepads (A/B work) and closes them + SDL_QuitSubSystem(GAMEPAD)
  afterwards so the game gets GAMEPAD_ADDED again; HUD box sized to its longest line (text overhung).
* **test15 (2026-11-06): ROM check screen on import** (romfind.c check_screen): own window, HUD font, CRC32
  of each ROM with a progress bar, rows FILE / SIZE / CRC32 / EXPECTED / STATUS (GOOD green; BAD CRC, BAD
  SIZE, MISSING red); Enter/A/click continue, Esc/B choose other ROMs. Expected CRCs/sizes come from
  romset.json via GAME_ROMS in game_config.h. romfind.log removed (diagnostics on stderr). test14: the
  user's earlier zip was bad; remade .zip and .7z both import fine (7z via tar.exe confirmed).
* **test14 (2026-11-06):** user's WinRAR zip was rejected on test13 (our inflate is fine: Python and Info-ZIP
  zips -1/-6/-9 all decode byte-identical). Changes: working folder = program folder at start and after
  the file dialog (rom_chdir_base), archive I/O through SDL_IOStream (UTF-8 paths), romfind.log written
  next to the exe with every step. If it still fails: read romfind.log.
* **test13 (2026-11-06): first-run ROM finder + JPEG artwork.** core/frontend/romfind.c: looks for <set>/,
  roms/<set>/, <set>.zip, roms/<set>.zip, <set>.7z, roms/<set>.7z; else a message box (Choose ROM file /
  folder / Quit) + SDL3 open dialogs. Zip: our own reader + inflate (tested: all 4 ROMs byte-identical from
  a deflate zip, 1.6 s). 7z: Windows %SystemRoot%\System32\tar.exe (libarchive), untested here; on failure
  the dialog asks again. Archives extract to assets/_rom (deleted after setup). image.c: PNG/BMP via
  SDL3, JPEG and other formats via WIC on Windows (links ole32, uuid, windowscodecs). User on test12:
  artwork pack looks good. Artwork/layout files are third-party: never commit or ship them.
* **test12 (2026-11-06): artwork packs.** core/frontend/layout.c reads MAME .lay bezel packs (elements with
  one image; views with screen/backdrop/overlay/bezel bounds; drawn backdrop, screen, overlay = MOD
  multiply, bezel = alpha; the view's box is fit to the output and the game goes in its screen rect).
  Config video.artwork = path to .lay or PNG (empty: auto Bezel/default.lay, artwork/default.lay);
  video.bezel = 0 off | 1 ours | 2.. artwork view N-1 (F1 menu cycles). User's pack (Mushihimesama 1.5,
  not ours, never commit; .gitignore covers Bezel/ artwork/ *.lay) names art1.png but ships 04.png: user
  must rename. Our bezel's glow (test11 "colour bleed" = per-row streaks) now a wide 7x3 average, 0.30.
* **test11 (2026-11-06): GPU presentation.** core/frontend/present.c: SDL3 GPU renderer forced to Vulkan
  (SDL_HINT_GPU_DRIVER), our GLSL shaders (core/frontend/shaders/game.frag, bezel.frag) -> SPIR-V by
  glslangValidator (apt glslang-tools) -> build/<set>/shaders_spv.h, run via SDL_GPURenderState (custom
  uniforms replace SDL's slot-0 constants; v_color loc 0, v_uv loc 1, sampler set 2, uniforms set 3).
  Fallback without Vulkan: plain renderer, no shaders (HUD shows BASIC vs VULKAN). Config [video]: scaling
  = fit | integer, shader = none | sharp | crt | crt-curved, bezel (procedural Yoko bezel with ambient
  glow), bezel_image (optional PNG/BMP overlay), uncapped (benchmark: no vsync, presents flat out; game
  frames still paced at 60.024 Hz). All in the F1 menu. HUD: GAME fps, PRESENT fps, EMU ms per game frame,
  CAP = 1000/EMU (game fps the CPU could sustain), PRES ms (draw+present; SDL_GPU has no timestamp
  queries, so no true GPU %). Window sized in pixels (SDL_WINDOW_HIGH_PIXEL_DENSITY + fit_window_pixels)
  for scaled 1440p/4K desktops. Untested here (no Vulkan in the container). **To confirm:** shaders and
  bezel look right, HUD says VULKAN, uncapped numbers, window size with display scaling.
* **test10 (2026-11-06, confirmed working by user incl. sound): test9 showed a black game area (HUD/menu fine, no sound heard).** Cause found: blit_ref
  writes 0 in the top byte and the texture was ARGB8888, which SDL3 blends -> invisible. Fix: XRGB8888 +
  SDL_BLENDMODE_NONE. Free play: still the coin trick (Start and Back both give a coin sound); native free play deferred by user.
* **test9 (2026-11-06): F:\claude2\fbl\futaribl_windows_test9.zip = SDL2 -> SDL3 port (SDL 3.4.20) + native rate.**
  Same SDL_Renderer presentation as test8 (no GPU shaders yet); audio now an SDL_AudioStream
  (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES 256), pads via SDL_EVENT_GAMEPAD_ADDED (also fires for pads present
  at startup; dedup kept). **To confirm on test9:** pads P1/P2, F1 menu (Guide, Back+Start), sound clean
  and low latency, vsync, fullscreen F11, HUD. Build: deps/SDL3-3.4.20 mingw (tag from `git ls-remote`;
  the GitHub API is blocked here); Linux frontend build now needs SDL3 (`pkg-config sdl3`), which Ubuntu
  24.04 lacks: build SDL3 from source if needed. Decided with user: **SDL3 GPU = Vulkan only (SPIR-V via
  glslang)**; next slice = SDL_GPU presenter (CRT, curved, bezels, integer / fill-keep-aspect, GPU % in HUD).
* **Native rate (2026-11-06, done, in test9):** "pcb" frame_rate option and CV1K_HW_FRAME_HZ removed;
  frame_rate = "native" (60.024 Hz) | "display"; old "mame"/"pcb" config values read as native; --pcb-rate gone.
* **User requests (2026-11-06, after test8):** (1) 60.024 Hz is the native rate (MAME: "measured from
  ibara PCB"); our 60.0184 was derived from an assumed 407x262 raster -> drop or rename the "pcb"
  frame_rate option (config.c, menu.c, cv1k.h CV1K_HW_FRAME_HZ, docs/hardware/cv1000.md). (2) After SDL3:
  scaling options = integer scale and scale-to-fill fullscreen (keep aspect). (3) Key and gamepad
  button remapping in the F1 menu (saved in config.toml). User on test8: looks good.
* **F1 settings menu (2026-11-06, test8):** core/frontend/menu.c, drawn with the HUD font (now full A-Z).
  F1 or gamepad Guide / Back+Start opens it; game paused while open; changes apply at once and are
  saved to config.toml on close. Items: orientation, window scale, fullscreen, vsync, game speed
  (mame 60.024 / pcb 60.018 / display = one frame per refresh on a ~60 Hz display, like MAME
  -syncrefresh; new config value frame_rate = "display", --sync-display), slowdown, free play, volume,
  rotate controls, HUD, resume, quit. Not yet: reset game (needs a cv1k teardown), key/pad remapping,
  video scaling filters/shaders (SDL3 step). (The --shot debug option was removed for the release.)
* **Free play redesign (user request):** today free play is a frontend trick (a Start press inserts a
  coin for 4 frames, then presses Start; input.c). Wanted: use the game's own FREE PLAY setting
  instead. Plan: find the settings block in NAND/EEPROM (the game keeps settings + high scores in
  NAND; MAME's test menu shows a Free Play option), locate the free-play byte by toggling it in
  MAME's service menu and diffing NAND/RAM, then have the frontend set it (config `freeplay`) and drop
  the coin injection. Keep `--coins` behaviour for coin-op mode.
* **Sync (2026-11-06): done for now.** MAME-parity fixes: stale DRC fetch PC, cv1k idle speedup (10 us spin,
  `run_headless --idle-pc 0C05176A`), VBLANK at k/60.024 s, IRQs/timeslice ends at MAME update points.
  Frames 502-700 frame-exact vs MAME (equal polling); unperturbed stage1 73 % identical flips, rest +-1
  VBLANK; fixed 2-frame shift from the first 29 boot frames. Details: docs/timing-sync-notes.md 6b/6c.
  Frontend not rebuilt: add cv1k_set_idle (GAME_IDLE_PC/RAM in game_config.h) before the next build.
* Gamepads P1/P2 confirmed working on test5 (user, 2026-11-06).
* **Sound next:** compare with MAME `-wavwrite` (same stage1.txt inputs, nvram deleted) against
  `run_headless ... --sound u23 u24 --wav`; pace emulation at 60.018 Hz independent of the display
  (now one emulated frame per vsync: >60 Hz displays run fast and drop audio); volume option.
* Then: F1 menu (reuse HUD font), SDL3 GPU presentation + CRT shaders + bezels, milestone 4 tests.
* Workspace setup: clone repo; stage F:\claude2\fbl\futaribl\* (4 files); pip install capstone
  --break-system-packages; apt-get install -y mingw-w64 zip lua5.4 glslang-tools; SDL3-devel-3.4.20-mingw.tar.gz
  from github.com/libsdl-org/SDL releases unpacked in /home/claude/deps; tools/build_game.sh futaribl
  <romdir> (~6.5 min, once; the "misses" number is cumulative), tools/build_frontend.sh futaribl
  windows (~7 min). Build note: core/sound is C++20 (final link with g++). Use mcp device tools for
  F:\claude2\fbl; device_commit_files can resend a stale copy if the staged file name is reused:
  give each new output a new file name.

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

- **Windows test build 2 (2026-11-05, futaribl_windows_test2.zip, commit 22eb817)**: user report: game
  runs, CLI switches work, smooth, feels similar to the original (no overlay yet to measure).

- **Performance HUD (2026-11-05)**: core/frontend/hud.c (own 5x7 font): FPS, CPU % (= emulation +
  software blitter time / 16.66 ms), EMU ms, frame time + max, 120-frame graph (frame time grey,
  emulation green, 16.7 ms line). F10 toggles; `[video] hud = true|false`, `--hud` / `--no-hud`.
  GPU % shows "--" until the SDL3 GPU layer (SDL2 renderer has no GPU timing). Windows build:
  futaribl_windows_test3.zip (includes today's timing fixes).

- **Sound, milestone 9 first cut (2026-11-05)**: core/sound/mpeg_audio.{cpp,h} (MAME decoder,
  unchanged except includes) + core/sound/ymz770.{cpp,h} (YMZ770 port, C API; YMZ774 left out).
  cv1k.c: writes to 0x10400000-7 render samples up to the write's CPU cycle first (one sample =
  6400 cycles = 102.4 MHz / 16 kHz, like MAME's stream update), output ring of 16384 stereo frames,
  cv1k_load_sound (u23/u24 word-swapped) / cv1k_load_sound_image (assets/sound.bin). run_headless
  `--sound u23 u24 --wav out.wav`. Frontend: setup writes assets/sound.bin (also created on start if
  an older assets/ lacks it), SDL audio 16 kHz S16 stereo, per-frame SDL_QueueAudio, frame audio
  dropped when > ~4 frames queued; `--no-sound`. Attract (no coin) is silent like the game's demo
  sound default; with stage1.txt the coin/menus/stage 1 produce audio (RMS ~1000-1500). Not yet
  verified against MAME `-wavwrite` (next: capture MAME wav with the same input script, compare
  sample-exact after aligning). Build: C++20 for the two sound files, final link with g++ (Windows:
  -static-libgcc -static-libstdc++; exe needs only SDL2.dll). Windows build: futaribl_windows_test4.zip.
  Known: one emulated frame per vsync, so on displays above 60 Hz the game and audio run fast (audio
  frames get dropped); proper fix = pace emulation to 60.018 Hz independent of the display.

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
1. **Sound (milestone 9)**: first cut done (see Done); remaining: compare with MAME -wavwrite, pacing/resampling for non-60 Hz displays, volume option. Original plan: YMZ770 (MAME src/devices/sound/ymz770.cpp + its MPEG-like decoder
   mpeg_audio.cpp; check licenses), sound ROMs u23/u24 (word-swapped) into assets/ at setup, SDL audio
   output; headless WAV dump for checking against MAME (`mame -wavwrite`).
2. GPU presentation layer (SDL3 GPU) + CRT/curved shaders + bezels.
3. F1 menu (reuse the HUD font in core/frontend/hud.c). Overlay done (F10).
4. **Stage-1 gameplay comparison (first try, 2026-11-04)**: MAME capture F:\claude2\fbl\mame_snaps_stage1
   (2400-4200 every 10, stage1.txt, fresh nvram). Ours with --frame-offset -1: MAME 2500 (mode select)
   identical; from 2600 (character select) on no exact match (closest differs by ~8.5k pixels at 2600;
   gameplay at 3200 is the same scene but a different state). Input offsets 0 and -2 give no matches
   either. **MAME is deterministic** (second capture mame_snaps_stage1b is identical to stage1 at
   2500/2600/3200/4000), so the RTC idea below is out and the difference is ours. New leading suspect:
   on-chip TMU timer counters (TCNT0-2 at 0xFFFFFE98/A4/B0, TSTR 0xFFFFFE92): ours never count, MAME's
   do, and games often seed RNG from a running timer at Start. Next: log on-chip register reads after
   frame 2260; implement TMU (MAME sh3comn.cpp / sh4tmu.cpp, BSD-3) if the game reads it.
   Findings (2026-11-04 late): game reads NO TMU registers after Start (only ports, DMAC, INTC, CCR,
   EXPEVT/TRA), so TMU is out. Input-offset sweep -3..+1: mode select always identical, character
   select always differs by the same 8,564 px -> not input timing. Blend modes used there are the same
   (s0d0, s0d1, s0d5, plain) already verified in attract. Working theory: game state divergence (e.g.
   an RNG advanced per idle-loop iteration / per CPU-time-dependent event) from residual timing error.
   Tooling to find it: `tools/mame/ramdump.lua` + `ramdump.bat` (+ shared `inputs.lua`, capture.lua now
   uses it too), `run_headless --dump-ram dir f1,f2,...`, `tools/ram_diff.py a.bin b.bin`.
   Frame mapping: ours = MAME + 1 (with --frame-offset -1). Next: diff MAME ram_02500..02600 vs ours
   ram_02501..02601, find the first differing variables, trace the code that writes them.
   **ROOT CAUSE FOUND (2026-11-04 night):** the game seeds its RNG with elapsed milliseconds:
   `srand([0xAC7FF000] + f(5))` at 0x0C066F90, where f(5) (0x0C056EDC) returns the ms counter at
   0x0C056EA0, incremented by the TMU1 1 kHz interrupt (TCOR1 = TCNT1 = 0x18FF at Pphi/4 = 6.4 MHz; TMU1
   callback 0x0C056EC8). TMU0 also runs at 1 kHz (priority 6, callback 0x0C062428, small). We had no
   TMU, so the counter stayed 0 and the seed differed (RNG state 0x0C1E3EB8 differs from frame 2500).
   **TMU now implemented** (cv1k.c: TSTR/TCOR/TCNT/TCR per MAME sh4tmu.cpp, TUNI0-2 via IPRA, level
   request until UNF cleared, live TCNT reads; CPU loops schedule timer events via cv1k_next_event /
   cv1k_timers). Our ms counter now runs at 1 kHz (39,301 at our frame 2511 vs MAME 39,130 at 2500).
   **New problem:** with the timers running we fall 10 frames further behind MAME during the memory test
   (MAME 221 -> 401: offset +2 -> -8; attract now -11 instead of -1). IRQ rate is right (2,000/s: both
   channels, as in MAME - IPRA is mapped for SH3 in MAME too), handlers + callbacks are short, SR IMASK
   is 0, game idles most of each frame. So the extra cost is not handler size. Next ideas: (1) measure
   cycles per interrupt in ours (entry -> rte) and per frame in the memory test; (2) MAME accepts IRQs
   only at DRC entry (CHECKIRQ) after a timeslice ends at the timer, and our backward-branch event checks
   return without FLUSH: check the pending-acc handling around interrupts (FLUSH vs keep vs DROP);
   (3) MAME's idle speedup (spin_until_time 10 us at idle PC 0x0C05176A) changes WHEN interrupts land
   relative to frame work; (4) get a MAME-side cycle/IRQ count via Lua (manager.machine.devices
   [':maincpu'] state, or a frame-by-frame RAM dump of the ms counter during the memory test).
   Goal: seed equality requires reaching srand in the same millisecond as MAME.
   **Timer-lag investigation (2026-11-05)** (debug instrumentation in run_headless, reverted afterwards):
   - Lag starts ~frame 240 (boot loading). Main loop at 0x0C063098 does ~1.2 frames of CPU work per
     iteration (loading, NAND DMA, decompressor 0x0C06D770 called from 0x0C06D438), then `wait(1)` at
     0x0C051764 (spin on vblank counter 0x0C002310, idle PC 0x0C05176A) -> iterations take 1 or 2 frames,
     so a ~1-2 % CPU increase pushes iterations past a vblank and loses whole frames (quantization).
     Frames 150-400: 149 iterations with TMU vs 155 without.
   - Only **TUNI0** matters: TUNI1 alone (ms counter, prio 1) gives exactly the no-TMU timeline. TUNI0
     (prio 6, IPRA 0x6100, callback 0x0C062428) is the 1 kHz sound-driver tick (cmd queue 0x0C062240,
     8 channels x 56 B at 0x0C062250); cost ~709 cycles/IRQ (TUNI1+vblank ~318) = ~0.7 % CPU.
   - Main-thread work per iteration (excl. IRQ time): 2.033M cycles with TUNI0 vs 2.019M without (+0.7 %).
     Decompressor cost per call is identical (1.47M) and its call count the same (33 vs 34).
   - Making IRQ handlers free (no cycles, no cache effects) removes about half the lag (equal at 400,
     still behind at 440). Remaining half = effect on the interrupted main thread.
   - Ruled out: RNG seed (forcing RNG=0 at 156 changes nothing), TCNT polling (game never reads TMU
     regs), DMA from IRQ (none), preemptive task switch (all IRQs return to SPC), sound gating (with
     stage1 inputs our sound queue = MAME: 0x134 commands at 2500, same request entry).
   - MAME check (sh4comn.cpp/sh4.h/sh4tmu.cpp, BSD-3): TUNI stays requested until UNF cleared (same as
     ours), source picked by highest priority (same), SH3 IRQ entry costs no cycles (same); MAME also
     writes EXPEVT for interrupts (we don't; game dispatches on INTEVT2 0xA4000000 so harmless);
     MAME timer fires after TCNT ticks (`ticks = tcnt`), ours after TCNT+1 (tiny).
   - Conclusion: MAME's main thread must have ~1 % more headroom per iteration; before TMU we matched
     MAME only because the missing IRQ load hid it. Candidates: (a) cost of resuming the main thread
     after an event exit (pending ACC kept across IRQ, then the handler's first not-taken branch DROPs
     it; re-entry fetch/cache-line state), (b) handler cost vs MAME (uncached/P4 register accesses in
     the handlers, vector path 0x8C000600 -> table 0x0C001000), (c) residual base-model error.
   - **MAME boot RAM dumps** (F:\claude2\fbl\mame_ram_boot, frames 150-400 every 10, fresh nvram):
     MAME's vblank counter 0x0C002310 and ms counter run exactly 1 frame ahead of ours (known -1
     offset); MAME seeds the boot RNG at ms 45 (0x2D), ours at 43 (0x2B). Best whole-RAM match
     offset (ours frame - MAME frame, exact match = 0 differing 16-byte blocks from 270 on):
     TMU on : 170-210 -1/-2, 220 -1, 230 0, 240 +1, 250 +2, 260 +3, 270 +4, 300 +5, 340 +7, 360-400 +8
     TMU off: 170-260 -2, 270-310 -1, 320-400 0
     0x0C051040 = game frame-flip counter (each +1 kicks a blitter list, 0x0C0510C4/0x0C0511E0):
     MAME renders 0.80 game frames per vblank in 200-280 (built-in slowdown), ours 0.76 (TMU off) /
     0.71 (TMU on). So the boot-screen frames need ~1 vblank of CPU work each and a 1-2 % timing error
     flips frames between 1 and 2 vblanks. **We were already slightly too slow before TMU** (fewer
     flips than MAME); TUNI0's ~1 % only made it visible. Target: find the ~1-2 % overcharge (data
     penalties are 28 % of all cycles in 220-270: 24.1M of 85.3M; fetch 1.1M; uncached accesses 679k).
   - Tooling: `run_headless --kick-log file` (emulated time of every blitter kick) and
     `--watch-log file addr,addr,...` (RAM words after every vsync) + `tools/mame/watchlog.lua/.bat`
     (same per-frame RAM-word log from MAME) + `tools/watch_diff.py mame ours` (first frame of every
     value of a progress counter, prints the offset each time it changes). A MAME Lua write tap on the
     blitter registers (0x18000000-7F, 64-bit program space) made MAME 0.289 exit right after
     install (no Lua error, no nvram written), so the kick-time logger was dropped on the MAME side.
     **Next: user runs `watchlog.bat stage1.txt 600` (nvram deleted), copies watch.txt to
     F:\claude2\fbl\mame_watch.txt; then `run_headless ... 600 --watch-log ours.txt
     0C051040,0C002310,0C056EA0` and `watch_diff.py`: shows which game frames take 2 vblanks in ours
     but 1 in MAME.**
   - **Per-game-frame work timing vs MAME (2026-11-05, flaglog.lua -> F:\claude2\fbl\mame_flags.txt):**
     main loop (0x0C063098) sets flag 0x0C1DE144 when a game frame is done, vblank handler 0x0C062F20
     kicks the flip and clears it. Boot screen = 4-frame cycle: 3 light frames (~8-9 ms work) + 1 heavy
     (~33 ms, just under 2 vblanks = 33.32 ms; a heavy frame over that spills to a 3rd vblank = the lag).
     Before fixes: light k%4=0 +1.2 %, k%4=1 **-5.6 % (-0.52 ms)**, k%4=2 0 %, heavy +0.63 %.
     **Fixed (MAME parity):** (1) DMAC: auto-request transfers set CHCR.TE (+DEI) 2*count+1 CPU cycles
     after the start, data/SAR/DAR/TCR immediately, no restart while in flight (sh4dmac.cpp) -> k%4=1
     frame now +1.1 %; (2) blitter busy-time model ported from cv1k_v.cpp (list walk: SRAM clocks for
     uploads, VRAM clocks for clipped draws, idle-op chunks, h-line fetch overhead; +0x10 ready bit
     reads 0 until done). Both only with timing on. RAM still matches MAME exactly from 270 on.
     **Now: every frame type ~+1 % (light +0.10 ms, heavy +0.21 ms)** -> heavy frames still spill;
     boot offset 160 +1 ... 270 +10 ... 360-400 +14 (worse than +8 because the missing DMA delay used to
     hide the overcharge). With TMU off: light +0.5 %, heavy -0.4 % -> our TMU interrupts cost ~1 %
     (per IRQ in 220-270: TUNI0 671 cycles = ~275 base + 257 data pen (95 acc, 8.8 miss) + 139 fetch
     pen (4.4 miss); TUNI1 137; vblank IRQ2 2623), MAME's apparently ~half. Next: find why MAME's
     interrupt cost is lower (pending ACC handling across the interrupt / DROP in the handler, IRQ
     entry/RTE costs, cache state) and the ~0.5 % fixed per-frame excess (light frames, TMU off).
   - **MAME checkints cycle drop (2026-11-05, fixed):** MAME's sh4 DRC sets compiler.checkints after
     RTE, LDC Rm,SR and LDC.L @Rm+,SR; the next generate_update_cycles (immediately after: LDCSR ends
     its sequence) does CHECKIRQ + HASHJMP *before* subtracting MAPVAR_CYCLES, so the pending base
     cycles (incl. the instruction's own, and RTE's slot) are never charged. Found with flaglog on the
     boot-screen loop 0x0C08D05A-0x0C08D0C4 (counter 0x0C7046A0; calls 0x0C05177C, which does
     ldc r0,sr, twice per iteration): MAME 127.25-127.31 cycles/iteration, ours 129.21 -> now 127.21.
     (Delay-slot cycles ARE charged by MAME: the slot is compiled with a temporary compiler state, but
     the subtraction uses MAPVAR_CYCLES, which the slot updates. Verified: removing them gave 120.2.)
     Result vs MAME per game frame: TMU off light +0.01-0.02 ms / heavy +0.025 ms (was +0.1); normal
     light +0.02-0.04 ms, heavy +0.12-0.18 ms (was +0.21); total +0.34 % (was +0.89 %). Boot offset
     now 202 +3 ... 358 +11 ... 589 +11 (was +14). Remaining: timer-interrupt overhead in heavy frames
     (MAME ~0.275 ms per heavy frame, ours ~0.37 ms; light frames both ~0.05 ms). In-handler cost is the
     same in light and heavy frames (TUNI0 ~690, TUNI1 ~135, IRQ2 ~2700 cycles), so it is either
     after-effects on the main thread (cache refills) or MAME's handlers being cheaper.
     New tools: tools/mame/pclog.lua (statistical PC profile; region attribution differs between MAME's
     update-point exits and our dispatch exits, so compare only self-contained code such as handlers),
     run_headless --pc-profile/--ipra0, tools/pc_diff.py; flaglog FLAG_IPRA0=1.
   - **Instruction traces vs MAME (2026-11-05):** tools/mame/trace.bat + trace_irq.cmd (MAME debugger
     `trace` with `{tracelog "%d ",totalcycles}` per instruction; note: `gtime 3830` did NOT stop at
     3.83 s, the trace started at totalcycles 1.47e9 = 14.4 s = attract frame ~863; use a cycle- or
     frame-based start next time) and `run_headless --interp --trace file f0 f1` (interpreter, same
     accounting as generated code). Same loop 0x0C08D188 in attract: MAME 120/145/164 cycles per
     iteration (hit / miss / miss+write-back), ours 120/145/164 -> per-instruction costs match.
     TUNI1 handler same 44-48-instruction path: MAME 53 cycles when all hits; differences come only
     from cache state at the interrupt. **Remaining heavy-frame excess (~0.12-0.18 ms with timer IRQs,
     ~0.025 ms without) = IRQ placement:** MAME takes a pending IRQ at the next update point (every
     memory-access flush / branch with allow_exception), ours only at backward branches and dispatch
     boundaries (we saw IRQ2 taken 13,838 cycles late), so handlers interrupt different code and
     disturb the cache differently. Next: add event checks at MAME's update points in gen_c.py (at
     least memory-access flushes and forward taken branches), measure the speed cost, then re-run
     flag_diff / watch_diff.
   Old suspect: MAME's RTC9701 starts at the host's current date/time (ours at 0) and the
   game may read it (RNG seed?) after Start. Check: (a) user runs the same MAME capture twice and we
   compare MAME vs MAME (if they differ, MAME is not deterministic here); (b) count RTC read commands
   (cmd 0x8) in our run after frame 2260; (c) if RTC matters, add a fixed RTC time option on both sides
   (MAME: Lua can't easily set it - may need -nvram with a prepared rtc file, or compare using our
   --rtc set to MAME's start time).
0. Stage-1 gameplay comparison: user runs (nvram deleted) `capture.bat 2400 4200 10 stage1.txt`,
   copies to F:\claude2\fbl\mame_snaps_stage1; ours: run_headless ... 4300 --input
   games/futaribl/inputs/stage1.txt --frame-offset -1 --png dir 1 --png-range 2350 4300; frame_check.
   Then user feedback on the Windows build (does it start, speed, frame pacing, controls).
1. Optional polish: the remaining 1-3 frame differences (first screen +3, then -1): suspects = interpreter lacks branch-target
   flushes during boot, MAME DRC block/sequence boundaries (max sequence length, page splits), IRQ
   acceptance points (MAME checks at update points), cache initial state.
2. Gameplay capture with inputs (now that capture.lua inputs work), blitter busy-time model.
3. Milestone 8 (Vulkan) / 9 (audio) / 11 (frontend).
