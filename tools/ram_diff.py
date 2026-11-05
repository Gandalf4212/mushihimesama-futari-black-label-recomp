#!/usr/bin/env python3
"""Compare two 8 MB CV1000 RAM dumps (MAME ramdump.lua vs run_headless --dump-ram).
Usage: ram_diff.py <a.bin> <b.bin> [--max N]   Prints differing ranges (address, length, sample bytes)."""
import sys
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
mx = int(sys.argv[sys.argv.index('--max') + 1]) if '--max' in sys.argv else 40
n = min(len(a), len(b)); ranges = []; i = 0
while i < n:
    if a[i] != b[i]:
        j = i
        while j < n and (a[j] != b[j] or (j + 16 < n and a[j:j + 16] != b[j:j + 16])): j += 1
        ranges.append((i, j - i)); i = j
    else: i += 1
total = sum(l for _, l in ranges)
print(f'{len(ranges)} differing ranges, {total} bytes')
for s, l in ranges[:mx]:
    print(f'  {0x0C000000 + s:08X} +{l:<6}  a={a[s:s + min(l, 8)].hex()}  b={b[s:s + min(l, 8)].hex()}')
