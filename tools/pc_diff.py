#!/usr/bin/env python3
"""Compare statistical PC profiles: MAME (tools/mame/pclog.lua) vs ours (run_headless --pc-profile).

Usage: pc_diff.py <mame_pcprof.txt> <our_pcprof.txt> [--top N]
Each line is "<256-byte region hex> <samples>". Shares are normalized to each run's total, so the
regions where ours spends a larger share of time than MAME (positive diff) are overcharged in ours
(or run more often). Prints the N (default 25) largest absolute differences, with a rough
significance (diff / sqrt(samples)).
"""
import math
import sys


def load(path):
    h = {}
    for line in open(path):
        p = line.split()
        if len(p) >= 2:
            h[int(p[0], 16)] = h.get(int(p[0], 16), 0) + int(p[1])
    return h


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 25
    if '--top' in sys.argv:
        args.remove(str(top))
    m, o = load(args[0]), load(args[1])
    tm, to = sum(m.values()), sum(o.values())
    print(f'samples: mame {tm}, ours {to}')
    rows = []
    for r in set(m) | set(o):
        sm, so = m.get(r, 0) / tm, o.get(r, 0) / to
        exp = sm * to
        sig = (o.get(r, 0) - exp) / math.sqrt(exp + 1)
        rows.append((abs(so - sm), r, sm, so, sig))
    rows.sort(reverse=True)
    print(' region      mame%    ours%    diff%   sigma')
    for _, r, sm, so, sig in rows[:top]:
        print(f'{r << 8:08x}  {sm * 100:7.3f}  {so * 100:7.3f}  {(so - sm) * 100:+7.3f}  {sig:+7.1f}')


if __name__ == '__main__':
    main()
