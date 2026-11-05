#!/usr/bin/env python3
"""Compare per-frame RAM-word logs: MAME (tools/mame/watchlog.lua) vs ours (run_headless --watch-log).

Usage: watch_diff.py <mame_watch.txt> <our_watch.txt> [--col N]
Each line is "<frame> <hex word> <hex word> ...". Column N (default 1 = first word, e.g. the game's
frame-flip counter 0x0C051040) is treated as a progress counter: for every value it takes, the first
frame it appears on is compared between the two runs. Prints the frame offset (ours - MAME) each time
it changes, so the frames where we lose (or gain) time stand out.
"""
import sys


def first_frames(path, col):
    seen = {}
    for line in open(path):
        p = line.split()
        if len(p) > col:
            v = int(p[col], 16)
            seen.setdefault(v, int(p[0]))
    return seen


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    col = int(sys.argv[sys.argv.index('--col') + 1]) if '--col' in sys.argv else 1
    if '--col' in sys.argv:
        args.remove(str(col))
    m, o = first_frames(args[0], col), first_frames(args[1], col)
    common = sorted(set(m) & set(o), key=lambda v: m[v])
    print(f'column {col}: {len(m)} values in MAME, {len(o)} in ours, {len(common)} common')
    print('   value  mame_frame  our_frame  offset')
    prev = None
    for v in common:
        d = o[v] - m[v]
        if d != prev:
            print(f'{v:8x}  {m[v]:10d}  {o[v]:9d}  {d:+6d}')
            prev = d
    if common:
        v = common[-1]
        print(f'{v:8x}  {m[v]:10d}  {o[v]:9d}  {o[v] - m[v]:+6d}  (last common value)')


if __name__ == '__main__':
    main()
