# Timing model (milestone 10) — design

Target: match MAME 0.289's `sh7709s.cpp` experimental cache/memory timing (BSD-3-Clause, Jesus Ramos),
so frame timing, loading duration and slowdown line up with MAME frame for frame. Real-PCB calibration
can come later (user's board is available but not easy to reach).

## Why it matters
Today `s->cycles` counts instructions (1 cycle each, memory free). Measured effect vs MAME on futaribl
attract: we run 290 frames ahead by MAME frame 1600, and drift +3 more during a loading phase before
MAME frame 2000. Slowdown (cache thrashing, uncached NAND reads, the needless cache flush in Cave's ROM
read routine) cannot appear at all until memory costs are modeled.

## MAME's model (what to reproduce)
- **Cache**: SH7709S unified cache, `SH7709S_CACHE_BLOCKS` sets × `SH7709S_CACHE_ASSOCIATIVITY` ways,
  `SH7709S_CACHE_LINE_SIZE` bytes, LRU per set, dirty bit per line (constants are in `sh7709s.h`: read
  them; hardware is 16 KB, 4-way, 16-byte lines). Regions 0xA… (P2) and 0xE… are uncacheable.
- **access_penalty(address, write)**, charged on every data read/write AND every instruction fetch:
  - hit: 0
  - miss, SDRAM (area 3, RAM `0x0C…`): `cpu = 1` (CACHE_MISS_STALL) and
    `bus = 3 (BUS) + 3 (BURST_READ_WORD) + rcd(MCR) + wcr2(area)`
  - non-SDRAM cacheable (ROM, area 0): `bus = (3 + wcr2) * line_fetch_count(area)` (area 0: 8)
  - uncached SDRAM: read `3 + rcd + wcr2`, write `3 + rcd + trwl`
  - uncached other (devices, NAND at area 4 `0xB0…`): `3 + wcr2`
  - `+ wcr1(area)` when the area differs from the last access
  - SDRAM page (1024 B) precharge: pending `tpc*2` cycles if the same page is hit right after a burst
  - write-back of an evicted dirty line costs `(3 + wcr1 + 4 + rcd + trwl) * 2` (+ `tpc*2` same page),
    paid by the next miss (`m_wb_active_cycles`), and drained 1/cycle by instruction fetches
  - result = `cpu + bus * 2` (bus clock is half the CPU clock)
- Register fields: WCR1/WCR2 (bus wait states per area), MCR (tpc, rcd, trwl, tras), BCR2 (bus width →
  line fetch count). The game programs these during boot via GBR-relative writes to `0xFFFFFFxx`
  (we already store them in `onchip[]`; addresses from the SH7709S manual / MAME sh3comn.cpp).
- Device accesses (NAND `0xB000000x`, sound `0xB040000x`, RTC `0xB0C0000x`, blitter `0xB800xxxx`) pay the
  same penalty function on uncached addresses. In MAME 0.289 this comes from the sh7709s.cpp DRC hooks
  (`drc_memory_access_read/write` on every memory access, `drc_update_icache` per group 0/4/15 opcode),
  not from cv1k.cpp (which has no penalty calls).
- Base instruction cycles: from MAME's SH DRC front end (`sh_fe.cpp` opcode cycles; most 1, mul/mac/
  div/ldc SR etc. more). Read `sh_fe.cpp` only for the cycle table.
- Other MAME timing to mirror: blitter busy time (`m_blit_delay_ns`, cv1k_v.cpp, from VRAM row
  accesses per op; the game polls +0x10 ready), NAND busy after read command (port E b5).

## How to implement it in the recompiler
1. `core/runtime/timing.c`: cache + penalty state machine (port of the logic above, our own code),
   `uint32_t tm_access(Timing *t, uint32_t addr, int write)` and `tm_fetch(Timing*, uint32_t pc)`.
2. Data accesses: the bus callbacks (`bus_rb/rw/rl/wb/ww/wl` in cv1k.c) call `tm_access` and add to
   `s->cycles`. Works unchanged for interpreter and generated code.
3. Instruction fetches: generated code must call `tm_fetch` per instruction (MAME does per instruction).
   Optimization that stays exact: a fetch is a hit when the previous fetch was in the same 16-byte line
   and no data access in between could have evicted it... but data accesses can evict, so the cheap exact
   rule is: per instruction, `if (line(pc) != t->last_fetch_line || t->evict_gen != t->fetch_gen)
   full lookup else hit`. Keep a `--timing exact|fast|off` switch: `off` = today's model (also the basis
   of the "no slowdown" option in milestone 11), `exact` = MAME parity.
4. Base cycles: add per-instruction base cost to the per-block constant in gen_c.py (from the table),
   so `s->cycles += N` becomes the sum of base costs; penalties add at run time.
5. The interpreter gets the same hooks (sh3_step calls tm_fetch; bus calls tm_access) so lockstep stays
   identical.

## Validation
- `frame_check.py --per-frame` over MAME attract capture: drift should go to a constant offset
  (ideally the boot offset becomes ~0 too, once boot runs with timing).
- Gameplay capture with inputs (heavy bullet scenes) to compare slowdown frame by frame.
- Performance budget: cache lookup per instruction fetch + per data access. Estimate 3–6× slower than
  today's 600 fps headless, still far above 60 fps. Profile and keep the `fast` path for fetches.

## "No slowdown" option (milestone 11)
Run with timing `off` for CPU cost while keeping vsync at 60.018 Hz: the game never misses a frame.
Note this changes game behavior (Cave games are designed around their slowdown), so it stays opt-in.
