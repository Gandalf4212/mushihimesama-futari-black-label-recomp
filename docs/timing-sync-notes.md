# Frame-speed / sync investigation vs MAME 0.289 (sessions of 2026-11-04 and 2026-11-05)

Goal: frame-exact timing against MAME 0.289 from power-on, including the game's own slowdown.
This file records everything tried, what each experiment showed, the tools, and the best guesses for
what is left. Numbers are from the futaribl boot / memory-check screen (frames ~150-420) unless noted.

## 1. Symptom

* Before the on-chip timers (TMU) existed, our screens matched MAME at a constant offset of -1 frame.
* After implementing the TMU (needed: the RNG seed at Start uses the TMU1-driven ms counter
  0x0C056EA0), we fell behind MAME: about 1 frame per 10-20 frames during the boot loading screen,
  +8..+14 frames by frame 360, with game RAM otherwise identical (only shifted in time).

## 2. Mechanism (understood)

* Boot screen = a 4-game-frame cycle: 3 light frames (~8-9 ms of CPU work each) + 1 heavy frame
  (~33 ms, just under 2 vblanks = 33.32 ms). Main loop 0x0C063098: work, then set the "frame ready"
  flag 0x0C1DE144, then `wait(1)` (0x0C051764, spins on vblank counter 0x0C002310) until the vblank
  IRQ handler (0x0C062F20) sees the flag, kicks the blitter flip (0x0C05108C, counter 0x0C051040)
  and clears it. A heavy frame that needs more than 33.32 ms spills into a 3rd vblank -> one whole
  frame lost. MAME's cadence is 1,1,1,2 vblanks per game frame; ours sometimes 1,1,1,3.
* So the visible lag is the vblank quantization of a heavy frame that is ~1 % too expensive.
  A ~0.2 ms error per heavy frame is enough.

## 3. Things ruled out (with evidence)

| suspect | how checked | result |
|---|---|---|
| RNG seed difference (TMU ms counter at srand) | forced RNG = 0 at frame 156 | no change in timeline |
| game polling TCNT | counted TMU register reads | game never reads TMU registers |
| DMA started from IRQs | counted DMA starts inside handlers | none |
| preemptive task switch in TUNI0 | every IRQ returns to its SPC | no switches |
| sound driver state / gating | MAME RAM at 2500 vs ours (with stage1 inputs) | identical sound queue (0x134 commands) |
| TMU period (TCOR vs TCOR+1) | MAME sh4_scale_up_mame_time = t*(n+1) | ours already TCOR+1 |
| peripheral clock | MAME MD pins -> mode 0 -> Pphi = clock/4 | matches ours |
| frame wait count N | logged 0x0C1DE148 | always 1 |
| NAND busy time | MAME nand_device has no timed busy | not modelled in MAME either |
| sound chip status | YMZ770 is write-only on CV1000 | no CPU-visible timing |
| cache model (index, LRU, flush) | line-by-line vs sh7709s.cpp | identical |
| access_penalty / wcr1/wcr2/MCR | line-by-line vs sh7709s.cpp | identical |
| delay-slot base cycles | removing them gave 120.2 vs MAME 127.3 cycles/iter | MAME charges them (MAPVAR_CYCLES) |
| extra event exits perturbing cycles | PC sampling via event_at | flag log byte-identical |

## 4. Fixes made (all MAME parity, all verified)

1. **DMAC completion delay** (sh4dmac.cpp): auto-request transfers move the data immediately but set
   CHCR.TE (and DEI) only 2*count+1 CPU cycles later; no restart while in flight. One boot frame type
   was 5.6 % (0.52 ms) too fast without it.
2. **Blitter busy time** (cv1k_v.cpp): list walk with SRAM clocks for uploads, VRAM clocks for clipped
   draws, idle-op chunks, h-line fetch overhead; +0x10 ready bit = 0 until done. No effect on the boot
   screen, but it is MAME behaviour and matters for gameplay slowdown.
3. **checkints cycle drop** (sh4.cpp): after RTE, LDC Rm,SR and LDC.L @Rm+,SR MAME sets
   compiler.checkints; the next generate_update_cycles does CHECKIRQ + HASHJMP *before* subtracting
   MAPVAR_CYCLES, so the pending base cycles (incl. the instruction's own and RTE's slot) are never
   charged. Boot loop 0x0C08D05A..0x0C08D0C4 (counter 0x0C7046A0): MAME 127.25-127.31 cycles/iter,
   ours 129.21 -> 127.21 after the fix.

Result now: total work +0.34 % vs MAME (was +0.89 %); with timer IRQs disabled on both sides
(IPRA = 0) +0.15 %, i.e. light frames +0.01-0.02 ms, heavy +0.025 ms. With timer IRQs: light
+0.02-0.04 ms, heavy **+0.12-0.18 ms** -> heavy frames still occasionally spill. Boot offset vs MAME:
+3 at 202 ... +11 at 358 ... +11 at 589 (was +14).

## 5. What is left and the evidence

* Timer-interrupt overhead in heavy frames: MAME ~0.275 ms per heavy frame (with minus without
  IRQs), ours ~0.37 ms. Light frames: both ~0.05 ms.
* Our in-handler cost is the same in light and heavy frames (TUNI0 ~690 cycles, TUNI1 ~135, vblank
  IRQ2 ~2700), i.e. the extra ~10k cycles per heavy frame are after-effects on the main thread or
  differences in what the handler finds in the cache.
* Instruction trace comparison (MAME debugger `trace` with totalcycles vs our `--interp --trace`):
  identical loop 0x0C08D188 in attract costs 120/145/164 cycles (hit / miss / miss+write-back) in
  both; the TUNI1 handler with all hits costs 53 cycles in both. Per-instruction accounting matches.

## 6. Best guesses going forward (in order)

1. **IRQ acceptance points.** MAME's DRC ends a timeslice at the first update point after the timer
   (generate_update_cycles with allow_exception: after memory accesses that flush, at forward and
   backward branches, at sequence ends) and takes the IRQ on re-entry. Ours only exits at backward
   branches and at call/return/jump dispatch boundaries; we saw a vblank IRQ taken 13,838 cycles
   late. Different acceptance points = handlers interrupt different code = different cache state
   for both the handler and the resumed main code. This is the leading suspect for the remaining
   heavy-frame difference. To try: in gen_c.py emit `if (s->cycles >= s->event_at) { s->pc = next;
   return; }` after every FLUSH that MAME does with allow_exception (memory accesses outside delay
   slots, taken forward branches, branch-target fall-ins), mirror it in the interpreter, rebuild,
   re-run flag_diff with and without IPRA0, and measure the speed cost (the check is a compare +
   predictable branch). Also check that the resume PC matches MAME's param (next pc).
2. **Where MAME's timeslice ends.** MAME runs the CPU until the next scheduled event (TMU, vblank,
   blitter/DMA timers, Lua timers). With `-nothrottle` and no Lua polling the slice boundaries can
   differ from the flaglog runs (flaglog's emu.wait timers add boundaries). Compare one flaglog run
   at POLL_US=5 with one at POLL_US=50: if MAME's numbers move, the polling perturbs MAME.
3. **Idle-loop speedup.** cv1k.cpp installs a speedup at the idle PC (spin_until_time / eat cycles in
   the vblank wait loop 0x0C05176A). It changes when MAME's CPU resumes after an IRQ during the wait
   (the wait loop is where most IRQs land in light frames). Read `install_speedups` / `speedup_r` in
   cv1k.cpp and model the same eat-cycles behaviour if it alters IRQ timing.
4. Smaller: tm_cache_array_write timing (the game's cache flush loop at 0x0C002A60 runs from P2),
   precise TE time for DMAs that complete while the CPU sleeps, blitter busy start time (MAME starts
   the timer at the exec write).

## 6b. Session 2026-11-06: stale fetch PC (the big one)

* MAME (cv1k `install_speedups` sets SH2DRC_FASTEST_OPTIONS) does not flush PC per instruction, so
  `sh7709s::drc_update_icache` charges the instruction fetch at a **stale** `m_sh2_state->pc`. That PC is
  only written at: timeslice ends (out_of_cycles: next pc), exception/IRQ/TRAPA entry (vector), RTE
  (SPC), checkints after LDC SR / LDC.L SR (pc+2), the pcflush address (idle pc + 2 = 0x0C05176C) and
  the nocode handler. JMP/JSR/RTS/BRAF/BSRF/BRA/BT/BF do **not** write it (hashjmp via ea / labels).
  With the debugger on PC is flushed every instruction, which is why the old trace comparisons matched.
* Modelled: SH3.fpc/pcflush/stale_fetch (sh3.h), SH3_FETCH in gen_c.py and sh3_step charge at fpc;
  cv1k_timers sets fpc = pc when an event is due. `run_headless --real-fetch` = old model,
  `--idle-pc 0C05176A` sets the pcflush (no measurable effect on boot), `--slice-us N` adds timeslice
  ends every N us (mimics a Lua emu.wait poll).
* Results: boot offset vs watch.txt +11 -> 0/-1 (pre-TMU baseline was -1). Stage1 3000 frames vs
  watch_stage1.txt (per-frame watchlog): old +13..+17, now -2..-4.
* **The flaglog references are perturbed**: flaglog's 5 us emu.wait polls end MAME timeslices and
  refresh the stale PC. Ours vs mame_flags: no slicing -1.36 %, --slice-us 5 -0.25 % (ipra0 -0.24 %),
  --slice-us 50 -0.69 %. So use watchlog (one poll per frame) as the reference from now on, and
  compare flaglog only against `--slice-us 5`.
* Idle speedup modelled (cv1k_set_idle; `run_headless --idle-pc 0C05176A`, ram var 0x0C002310 hardcoded
  in run_headless for now): a 16/32-bit read of idle_ram..+7 while fpc == idle_pc or idle_pc+2 ends
  the timeslice and jumps cycles +1024 (10 us); IRQs are taken after the spin.
* Result with stale fetch + idle spin: boot offset vs watch.txt a **constant +1 from the first flip
  (frame 202 vs 203) to 599**, no drift. Stage1 vs watch_stage1.txt: +0..+2 during play, 0 at 3000; with
  a 1-frame shift the flip counter 0x0C051040 is identical on 2310/2851 frames (81 %).
* Left: (a) the constant +1 that already exists at the first flip (boot/power-on, before the main
  loop; look at the boot interpreter run and when MAME's first vblank lands); (b) the 1-2 frame jitter
  in gameplay slowdown (IRQ acceptance points 6.1; our timeslice ends are at backward branches and
  dispatch returns, MAME's at update points); (c) the frontend does not call cv1k_set_idle yet (add
  GAME_IDLE_PC/GAME_IDLE_RAM to build_frontend.sh's game_config.h and call it in main_sdl.c).

## 6c. Session 2026-11-06 (later): VBLANK schedule, update points, where we stand

* VBLANK: MAME 0.289 cv1k uses set_refresh(60.024 Hz). Measured with flaglog (mame_flags_500.txt): the
  flip at frame 501 is at 8.346676 s = 501.000 / 60.024 + handler time, i.e. VBLANK k at k / 60.024 s
  (a first guess of "line 240 of 512" was wrong: it put every IRQ 7.8 ms late). Now
  `cv1k_vsync_at(k)` = (k + 1) / 60.024; 60.024 Hz is also the native rate (MAME: measured on an Ibara PCB); the derived 60.0184 Hz "pcb" option was removed 2026-11-06.
  Watch-log frame labels now mean the same time on both sides (no shift).
* IRQ acceptance / timeslice ends at MAME update points (gen_c.py): UPDATE_POINT(PC + 2) after every
  flushing memory op, the event check on forward as well as backward in-function branches and at
  sequence fall-ins; none on not-taken conditional branches (MAME drops those cycles). The
  interpreter still allows an exit after every instruction (not mirrored yet). Cost: stage1 3000 frames
  headless in 26 s (~115 fps).
* **Frames 502-700 with equal 5 us polling on both sides** (`run_headless --slice-from 502 --slice-us 5`
  vs flaglog 500-700): all 151 MAME flips on the same VBLANK in ours (frame-exact). Work time: light
  frames -0.001 ms, heavy -0.022 ms mean (-0.06 %).
* Unperturbed stage1 (watch_stage1.txt, no shift): flip counter equal on 2072/2851 frames, otherwise
  +-1 (one VBLANK) on frames whose work ends very close to a VBLANK. The game's vblank counter
  0x0C002310 is a constant -2 vs MAME: our main loop starts 2 VBLANKs later.
* That constant comes from the first ~29 frames of boot (program copy from the boot ROM, on the
  interpreter): markers (watch_boot.txt) MAME 5/18/28/29/45, ours 6/20/29/31/47; after frame 29
  every phase is in lockstep (+2). `--real-fetch` gives 6/19/29/30/46, so MAME refreshes its stale
  PC more often during boot than we do (unknown why: other scheduler events? nocode handler on
  first execution of each block?). Effect: a fixed 2-frame shift at power-on, no effect on speed.
* Status: good enough to move on. Leftovers if ever needed: the boot refresh points above, mirroring
  update points in the interpreter, and heavy frames 0.02 ms fast.

## 7. Tools (all in the repo)

| tool | what |
|---|---|
| `tools/mame/flaglog.lua/.bat` | poll a RAM word every N us (coroutine + emu.wait), log changes with emulated time; `FLAG_IPRA0=1` disables timer IRQs from frame 150 |
| `run_headless --change-log file addr` | same log from ours |
| `tools/flag_diff.py mame ours` | per-game-frame work time (flag 1->0 to next 0->1) |
| `tools/mame/watchlog.lua/.bat`, `--watch-log`, `tools/watch_diff.py` | per-frame RAM words; frame offset of a progress counter (0x0C051040) |
| `tools/mame/pclog.lua/.bat`, `--pc-profile`, `tools/pc_diff.py` | statistical PC profile (attribution differs, compare only self-contained code) |
| `tools/mame/trace.bat` + `trace_irq.cmd`, `--interp --trace file f0 f1` | per-instruction cycle traces (NOTE: `gtime 3830` did not stop at 3.83 s; the trace started at totalcycles 1.47e9 = attract frame ~863 - use a cycle/frame based start) |
| `tools/mame/ramdump.lua/.bat`, `--dump-ram`, `tools/ram_diff.py` | RAM snapshots |
| `run_headless --ipra0`, `--kick-log` | IRQ-off experiments, blitter kick times |
| removed: `kicklog.lua` | a Lua write tap on the blitter (0x18000000-7F) made MAME 0.289 exit silently |

MAME reference data on the user's PC (F:\claude2\fbl): mame_flags.txt, mame_flags_ipra0.txt.txt,
watch.txt, mame_loop_ipra0.txt, mame_pcprof.txt, mame_pcprof_ipra0.txt, mame_trace.txt,
mame_ram_boot\ (frames 150-400 every 10), mame_ram\ (2500-2600 with stage1 inputs).
Always delete F:\mame0.289\nvram\futaribl before a capture.
