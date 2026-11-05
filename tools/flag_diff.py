#!/usr/bin/env python3
"""Compare per-game-frame work durations: MAME (tools/mame/flaglog.lua) vs ours (run_headless --change-log).

Usage: flag_diff.py <mame_flags.txt> <our_flags.txt> [--all]
Both files log changes of the main loop's "frame ready" flag (futaribl: 0x0C1DE144) as
"<frame> <seconds> <hex value>". A 1->0 change is the flip (vblank handler), the next 0->1 change is
the main loop finishing the following game frame. The k-th work interval of each run is assumed to be
the same game frame. Prints, per game frame, the work time in both runs and the difference; with
--all every frame, otherwise only frames where ours differs from MAME by more than 0.5 %.
"""
import sys


def intervals(path):
    ev = []
    for line in open(path):
        p = line.split()
        if len(p) >= 3:
            ev.append((int(p[0]), float(p[1]), int(p[2], 16)))
    out = []
    for i in range(1, len(ev)):
        if ev[i - 1][2] != 0 and ev[i][2] == 0:            # flip
            for j in range(i + 1, len(ev)):
                if ev[j][2] != 0:                           # work done
                    out.append((ev[i][0], ev[i][1], ev[j][1] - ev[i][1]))
                    break
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    show_all = '--all' in sys.argv
    m, o = intervals(args[0]), intervals(args[1])
    n = min(len(m), len(o))
    print(f'work intervals: mame {len(m)}, ours {len(o)}; comparing {n}')
    print('    k  mame_frame our_frame  mame_ms   ours_ms   diff_ms   diff%')
    tm = to = 0.0
    for k in range(n):
        (mf, _, mw), (of, _, ow) = m[k], o[k]
        tm += mw; to += ow
        pct = (ow - mw) / mw * 100 if mw else 0.0
        if show_all or abs(pct) > 0.5:
            print(f'{k:5d}  {mf:9d} {of:9d}  {mw * 1e3:8.3f}  {ow * 1e3:8.3f}  {(ow - mw) * 1e3:+8.3f}  {pct:+6.2f}')
    if n:
        print(f'total work: mame {tm * 1e3:.1f} ms, ours {to * 1e3:.1f} ms ({(to - tm) / tm * 100:+.2f} %)')


if __name__ == '__main__':
    main()
