#!/usr/bin/env python3
"""Static code discovery over a CV1000 RAM image.

Usage: discover.py <ram.bin> <game.json> <out.json> [--extra hints.txt]
Finds functions and basic-block starts by recursive descent from:
  - game.json boot.main_entry, VBR+0x100/0x400/0x600, every RAM pointer in boot.vector_table
  - call targets (bsr, and jsr/bsrf whose register was loaded from a PC-relative literal)
  - optional extra addresses (one hex address per line; e.g. collected from runtime misses)
Addresses are physical (alias bits stripped). Output JSON:
  {"functions": {entry: [block_start, ...]}, "indirect": [pc, ...], "stats": {...}}
"""
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from isa import ISA, build_decode_table, static_target

RAM_BASE = 0x0C000000
TABLE = build_decode_table()


def phys(a):
    return a & 0x1FFFFFFF


class Image:
    def __init__(self, data):
        self.d = data
        self.end = RAM_BASE + len(data)

    def ok(self, a):
        return RAM_BASE <= a < self.end - 1 and not a & 1

    def w16(self, a):
        o = a - RAM_BASE
        return struct.unpack('>H', self.d[o:o + 2])[0]

    def w32(self, a):
        o = a - RAM_BASE
        return struct.unpack('>I', self.d[o:o + 4])[0]


def kind_of(op):
    i = TABLE[op]
    return None if i < 0 else ISA[i][2]


def walk_function(img, entry, call_targets, indirect, literals=None, strict=False):
    """Recursive descent inside one function. Returns sorted list of block starts."""
    blocks, seen, work = {entry}, set(), [entry]
    while work:
        pc = work.pop()
        lit = {}  # register -> literal value loaded in this block (for jsr @Rn)
        while True:
            if pc in seen or not img.ok(pc):
                break
            seen.add(pc)
            op = img.w16(pc)
            idx = TABLE[op]
            if idx < 0:
                if strict:
                    return None, None
                break  # illegal: data reached; stop this path
            pat, mn, kind, _ = ISA[idx]
            if mn == 'mov.l @(disp,PC),Rn':
                a = ((pc + 4) & ~3) + (op & 0xFF) * 4
                if img.ok(a):
                    lit[(op >> 8) & 15] = img.w32(a)
                    if literals is not None:
                        literals.add(phys(img.w32(a)))
            elif kind == 'op' and (op >> 8) & 15 in lit and mn.endswith('Rn'):
                lit.pop((op >> 8) & 15, None)  # register overwritten (conservative)
            if kind == 'op':
                pc += 2
                continue
            tgt = static_target(ISA[idx], op, pc)
            slot = pc + 2
            if kind in ('cbr', 'cbrs'):
                nxt = pc + 2 if kind == 'cbr' else pc + 4
                for t in (tgt, nxt):
                    t = phys(t)
                    if img.ok(t):
                        blocks.add(t); work.append(t)
                if kind == 'cbrs':
                    seen.add(slot)
                break
            seen.add(slot)
            if kind == 'br':
                t = phys(tgt)
                if img.ok(t):
                    blocks.add(t); work.append(t)
                break
            if kind in ('call', 'icall'):
                if kind == 'call':
                    call_targets.add(phys(tgt))
                else:
                    r = (op >> 8) & 15
                    if mn.startswith('jsr') and r in lit and img.ok(phys(lit[r])):
                        call_targets.add(phys(lit[r]))
                    else:
                        indirect.add(pc)
                ret = pc + 4  # execution resumes here after the callee's rts
                blocks.add(ret); work.append(ret)
                break
            if kind == 'ijmp':
                r = (op >> 8) & 15
                if mn.startswith('jmp') and r in lit and img.ok(phys(lit[r])):
                    call_targets.add(phys(lit[r]))  # tail call
                else:
                    indirect.add(pc)
                break
            break  # ret, rte, trap, sleep
    return sorted(blocks), seen


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    img = Image(open(args[0], 'rb').read())
    game = json.load(open(args[1]))
    boot = game['boot']
    entries = {phys(int(boot['main_entry'], 16))}
    vbr = phys(int(boot['vbr'], 16))
    entries |= {vbr + 0x100, vbr + 0x400, vbr + 0x600}
    vt = boot.get('vector_table')
    if vt:
        a = phys(int(vt['addr'], 16))
        for k in range(int(vt['size'], 16) // 4):
            v = phys(img.w32(a + 4 * k))
            if img.ok(v):
                entries.add(v)
    if '--extra' in sys.argv:
        for line in open(sys.argv[sys.argv.index('--extra') + 1]):
            line = line.split('#')[0].strip()
            if line:
                entries.add(phys(int(line, 16)))

    functions, indirect, done, covered, literals = {}, set(), set(), set(), set()
    # pointer scan: aligned words anywhere in the image that point into the code region
    code_lo = RAM_BASE
    code_hi = phys(int(game['boot'].get('code_end', '0x0C300000'), 16))
    for o in range(0, len(img.d) - 3, 4):
        v = phys(struct.unpack_from('>I', img.d, o)[0])
        if code_lo <= v < code_hi and not v & 1:
            literals.add(v)
    rejected = set()
    work = [(e, False) for e in sorted(entries)]
    while True:
        while work:
            e, strict = work.pop()
            if e in done or not img.ok(e):
                continue
            calls, ind = set(), set()
            blocks, seen = walk_function(img, e, calls, ind, literals, strict)
            if blocks is None:
                rejected.add(e); continue
            done.add(e)
            functions[e] = blocks
            covered |= seen
            indirect |= ind
            work.extend((c, False) for c in calls if c not in done)
        # literal-pool values that point into RAM and are not inside already-decoded instructions
        cand = [v for v in literals if img.ok(v) and v not in done and v not in rejected and v not in covered]
        if not cand:
            break
        work = [(v, True) for v in sorted(cand)]

    out = {
        'functions': {f'{k:08X}': [f'{b:08X}' for b in v] for k, v in sorted(functions.items())},
        'indirect': [f'{p:08X}' for p in sorted(indirect)],
        'stats': {'functions': len(functions), 'blocks': sum(len(v) for v in functions.values()),
                  'code_bytes': 2 * len(covered), 'indirect_sites': len(indirect), 'rejected_literals': len(rejected)},
    }
    json.dump(out, open(args[2], 'w'), indent=1)
    print(json.dumps(out['stats']))


if __name__ == '__main__':
    main()
