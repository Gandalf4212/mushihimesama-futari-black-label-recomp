#!/usr/bin/env python3
"""Compare our frames with MAME reference snapshots.

Usage: frame_check.py <our_dir> <mame_dir> [--diff out.png] [--per-frame]
  our_dir:  frame_NNNNN.png from run_headless --png (our frame numbers)
  mame_dir: f_NNNNN.png from tools/mame/capture.lua (MAME frame numbers)
Finds the frame offset by matching exact pixel hashes, then reports how many aligned frames match
exactly, the first mismatching frame and its differing pixel count (optionally writes a diff image).
"""
import glob, hashlib, os, re, struct, sys, zlib

def read_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', path
    i, idat, w = 8, b'', None
    while i < len(d):
        n = struct.unpack('>I', d[i:i + 4])[0]; t = d[i + 4:i + 8]; body = d[i + 8:i + 8 + n]
        if t == b'IHDR': w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
        elif t == b'IDAT': idat += body
        i += 12 + n
    bpp = {2: 3, 6: 4}[ctype]
    raw = zlib.decompress(idat); stride = w * bpp
    out = bytearray(); prev = bytearray(stride); p = 0
    for _ in range(h):
        f = raw[p]; line = bytearray(raw[p + 1:p + 1 + stride]); p += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0; b = prev[x]; c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        out += line if bpp == 3 else bytes(v for k, v in enumerate(line) if k % 4 != 3)
    return w, h, bytes(out)

def frames(d, pat):
    r = {}
    for p in glob.glob(os.path.join(d, '*.png')):
        m = re.search(pat, os.path.basename(p))
        if m: r[int(m.group(1))] = p
    return r

def write_png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))

def main():
    ours = frames(sys.argv[1], r'frame_(\d+)\.png'); mame = frames(sys.argv[2], r'f_(\d+)\.png')
    print(f'{len(ours)} of our frames, {len(mame)} MAME frames')
    oh = {}; data = {}
    for k, p in ours.items():
        w, h, px = read_png(p); data[('o', k)] = (w, h, px); oh.setdefault(hashlib.sha1(px).hexdigest(), []).append(k)
    offsets = {}
    for k, p in sorted(mame.items()):
        w, h, px = read_png(p); data[('m', k)] = (w, h, px)
        for ok in oh.get(hashlib.sha1(px).hexdigest(), []):
            offsets[k - ok] = offsets.get(k - ok, 0) + 1
    if not offsets:
        print('no identical frames at any offset'); return 1
    if '--per-frame' in sys.argv:
        # drift curve: for each MAME frame, which of our frames is identical (offset = MAME - ours)
        prev = None
        for k in sorted(mame):
            hits = oh.get(hashlib.sha1(data[('m', k)][2]).hexdigest(), [])
            offs = sorted(k - o for o in hits)
            near = min(offs, key=lambda x: abs(x - prev)) if offs and prev is not None else (offs[0] if offs else None)
            flag = '' if near is not None else '   NO MATCH'
            if near is not None and prev is not None and near != prev: flag = f'   drift {near - prev:+d}'
            print(f'MAME {k:5d}: offset {near}{flag}')
            if near is not None: prev = near
    off, cnt = max(offsets.items(), key=lambda kv: kv[1])
    print(f'best offset: MAME frame = our frame + {off}  ({cnt} identical frames at this offset)')
    first_bad = None; match = total = 0
    for k in sorted(mame):
        o = k - off
        if ('o', o) not in data: continue
        total += 1
        if data[('m', k)][2] == data[('o', o)][2]: match += 1
        elif first_bad is None: first_bad = k
    print(f'aligned frames: {total}, identical: {match}')
    if first_bad is not None:
        w, h, a = data[('m', first_bad)]; _, _, b = data[('o', first_bad - off)]
        n = sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])
        print(f'first mismatch: MAME frame {first_bad} (ours {first_bad - off}): {n} pixels differ')
        if '--diff' in sys.argv:
            diff = bytes(255 if a[i] != b[i] or a[i + 1] != b[i + 1] or a[i + 2] != b[i + 2] else a[i] // 4 for i in range(0, len(a), 3) for _ in range(3))
            write_png(sys.argv[sys.argv.index('--diff') + 1], w, h, diff)
    return 0 if first_bad is None and total else 1

if __name__ == '__main__':
    sys.exit(main())
