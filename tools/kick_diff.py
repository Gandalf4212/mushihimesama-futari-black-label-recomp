#!/usr/bin/env python3
"""Compare blitter-kick timelines: MAME (tools/mame/kicklog.lua) vs ours (run_headless --kick-log).

Usage: kick_diff.py <mame_kicks.txt> <our_kicks.txt> [--every N]
Both files hold one line per blitter kick: "<frame> <emulated seconds>". The n-th kick of each run
is assumed to be the same game frame, so the time difference shows how far ahead (-) or behind (+)
we are. Prints every N-th kick (default 10) and each kick where the difference jumps by more than
half a video frame, so the phase where we lose time stands out. 'phase' is the time since the
last frame boundary (frames counted from power-on), i.e. roughly how much CPU work came before the kick;
MAME's vblank may sit at a constant offset from ours, so compare how phases change, not absolute values.
"""
import sys

FRAME_S = 1.0 / 60.0183806291


def load(path):
    out = []
    for line in open(path):
        p = line.split()
        if len(p) >= 2:
            out.append((int(p[0]), float(p[1])))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    every = int(sys.argv[sys.argv.index('--every') + 1]) if '--every' in sys.argv else 10
    if '--every' in sys.argv:
        args.remove(str(every))
    mame, ours = load(args[0]), load(args[1])
    n = min(len(mame), len(ours))
    print(f'kicks: mame {len(mame)}, ours {len(ours)}; comparing {n}')
    print(' kick  mame_frame  our_frame   mame_s       ours_s     diff(frames)  gap mame/ours (ms)  phase mame/ours (ms)')
    prev = None
    for i in range(n):
        (mf, mt), (of, ot) = mame[i], ours[i]
        d = (ot - mt) / FRAME_S
        gm = (mt - mame[i - 1][1]) * 1e3 if i else 0.0
        go = (ot - ours[i - 1][1]) * 1e3 if i else 0.0
        jump = prev is not None and abs(d - prev) > 0.5
        if i % every == 0 or jump or i == n - 1:
            print(f'{i:5d}  {mf:10d}  {of:9d}  {mt:11.6f}  {ot:11.6f}  {d:+10.3f}  {gm:8.3f}/{go:8.3f}  {(mt % FRAME_S) * 1e3:7.3f}/{(ot % FRAME_S) * 1e3:7.3f}{"  <-- jump" if jump else ""}')
        prev = d


if __name__ == '__main__':
    main()
