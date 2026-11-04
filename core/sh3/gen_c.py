#!/usr/bin/env python3
"""Generate C from a RAM image + discovery.json, reusing isa.py instruction bodies.

Usage: gen_c.py <ram.bin> <discovery.json> <out_dir> [--per-file N]
Writes out_dir/recomp_NNN.c and out_dir/recomp_table.c (address -> function for every block start).

Execution model (see docs/recompiler.md):
  - Each discovered function becomes `void f_XXXXXXXX(SH3 *s)`, entered through a dispatcher that may
    enter at any block start (switch on s->pc). Calls, returns and indirect jumps set s->pc and return
    to the dispatcher, so the C stack never depends on guest PR/stack tricks (task switches are safe).
  - Branches inside a function are gotos. Backward gotos check the event deadline (interrupts/timing).
  - s->cycles += <instructions in block> at each block start (instruction-count clock; the real timing
    model replaces these constants later).
"""
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from isa import ISA, build_decode_table, static_target

FLUSH_FORMS = None

def base_cycles(op):
    s = op >> 12
    if s == 0:
        lo8 = op & 0xFF
        if lo8 in (0x0B, 0x23): return 2
        if lo8 == 0x1B: return 3
        if lo8 == 0x2B: return 4
        if op & 0xF == 7: return 2
        if op & 0xF == 0xF: return 3
        return 1
    if s == 2: return 2 if op & 0xF in (0xE, 0xF) else 1
    if s == 3: return 2 if op & 0xF in (5, 0xD) else 1
    if s == 4:
        if op & 0xF == 0xF: return 3
        return {0x03: 2, 0x07: 3, 0x1B: 4}.get(op & 0xFF, 1)
    if s == 8:
        k = (op >> 8) & 0xF
        return 3 if k in (9, 0xB) else 2 if k in (0xD, 0xF) else 1
    if s in (0xA, 0xB): return 2
    if s == 0xC: return 8 if (op >> 8) & 0xF == 3 else 1
    return 1

def is_flush(op):
    g, lo, lo8 = op >> 12, op & 0xF, op & 0xFF
    if g == 0: return lo in (4, 5, 6, 0xC, 0xD, 0xE)
    if g in (1, 5, 9, 0xD): return True
    if g in (2, 6): return lo <= 2 or 4 <= lo <= 6
    if g == 4: return lo8 in (0x02, 0x12, 0x22, 0x03, 0x13, 0x23, 0x06, 0x16, 0x26, 0x07, 0x17, 0x27, 0x1B)
    if g == 8: return (op >> 8) & 0xF in (0, 1, 4, 5)
    if g == 0xC: k = (op >> 8) & 0xF; return k <= 2 or 4 <= k <= 6
    return False


RAM_BASE = 0x0C000000
TABLE = build_decode_table()


def fields(pat, op):
    out = []
    for ch in 'nmid':
        idx = [k for k, c in enumerate(pat) if c == ch]
        if not idx:
            continue
        sh, mask = 15 - idx[-1], (1 << len(idx)) - 1
        v = (op >> sh) & mask
        if ch in 'nm':
            out.append(f'const int {ch} = {v}; (void){ch};')
        elif ch == 'i':
            out.append(f'const uint32_t IMM = {v}u; const int32_t SIMM = {v - 256 if v & 0x80 else v}; (void)IMM; (void)SIMM;')
        else:
            name = {15: 'D4', 255: 'D8', 4095: 'D12'}[mask]
            out.append(f'const uint32_t {name} = {v}u; (void){name};')
    return ' '.join(out)


def body_for(op, pc):
    """C statement block for a plain instruction at pc (no control flow)."""
    pat, mn, kind, body = ISA[TABLE[op]]
    return body_slot(op, pc, False)


def body_slot(op, pc, in_slot):
    pat, mn, kind, body = ISA[TABLE[op]]
    acct = f'ACC({base_cycles(op)});' + ('' if in_slot or not is_flush(op) else ' FLUSH();')
    return f'{{ const uint32_t PC = hi | 0x{pc:08X}u; (void)PC; SH3_FETCH(0x{op:04X}u, PC); {fields(pat, op)} {body} {acct} }} /* {mn} */'


class Gen:
    def __init__(self, ram):
        self.d = ram

    def w16(self, a):
        o = a - RAM_BASE
        return struct.unpack('>H', self.d[o:o + 2])[0]

    def goto(self, tgt, cur_pc, blockset):
        if tgt in blockset:
            if tgt <= cur_pc:
                return f'{{ if (s->cycles >= s->event_at) {{ s->pc = hi | 0x{tgt:08X}u; return; }} goto L_{tgt:08X}; }}'
            return f'goto L_{tgt:08X};'
        return f'{{ s->pc = hi | 0x{tgt:08X}u; return; }}'

    def slot(self, pc):
        op = self.w16(pc)
        idx = TABLE[op]
        if idx < 0 or ISA[idx][2] != 'op':
            return f'sh3_bad_slot(s, hi | 0x{pc:08X}u);'
        return body_slot(op, pc, True)

    def function(self, entry, blocks):
        blockset = set(blocks)
        out = [f'void f_{entry:08X}(SH3 *s) {{', '    const uint32_t hi = s->pc & 0xE0000000u;',
               '    switch (s->pc & 0x1FFFFFFFu) {']
        out += [f'    case 0x{b:08X}u: goto L_{b:08X};' for b in blocks]
        out += ['    default: sh3_miss(s); return;', '    }']
        for b in blocks:
            lines, pc, count = [], b, 0
            while True:
                op = self.w16(pc)
                idx = TABLE[op]
                if idx < 0:
                    lines.append(f'sh3_miss_at(s, hi | 0x{pc:08X}u); return;')
                    break
                pat, mn, kind, body = ISA[idx]
                if kind == 'op':
                    lines.append(body_for(op, pc)); count += 1; pc += 2
                    if pc in blockset:
                        lines.append(f'FLUSH(); goto L_{pc:08X};')
                        break
                    continue
                PCc = f'(hi | 0x{pc:08X}u)'
                lines.append(f'SH3_FETCH(0x{op:04X}u, {PCc}); ACC({base_cycles(op)});')
                tgt = static_target(ISA[idx], op, pc)
                tgt = tgt & 0x1FFFFFFF if tgt is not None else None
                r = (op >> 8) & 15
                if kind == 'cbr':
                    cond = 'T' if mn.startswith('bt') else '!T'
                    count += 1
                    lines.append(f'if ({cond}) {{ FLUSH(); {self.goto(tgt, pc, blockset)} }}')
                    lines.append('DROP(); ' + self.goto(pc + 2, pc, blockset))
                elif kind == 'cbrs':
                    cond = 'T' if mn.startswith('bt') else '!T'
                    count += 2
                    lines.append(f'{{ const int take = ({cond}) != 0; if (take) {{ {self.slot(pc + 2)} FLUSH(); {self.goto(tgt, pc, blockset)} }} }}')
                    nxt = self.w16(pc + 2)
                    plain = TABLE[nxt] >= 0 and ISA[TABLE[nxt]][2] == 'op'
                    lines.append('DROP(); ' + (body_slot(nxt, pc + 2, False) + ' ' + self.goto(pc + 4, pc, blockset) if plain else self.goto(pc + 2, pc, blockset)))
                else:
                    count += 2
                    if kind == 'br':
                        lines.append(f'{self.slot(pc + 2)} FLUSH(); {self.goto(tgt, pc, blockset)}')
                    elif kind == 'call':
                        lines.append(f'PR = {PCc} + 4; {self.slot(pc + 2)} FLUSH(); s->pc = hi | 0x{tgt:08X}u; return;')
                    elif kind in ('icall', 'ijmp'):
                        t = f'{PCc} + 4 + s->r[{r}]' if mn.startswith(('braf', 'bsrf')) else f's->r[{r}]'
                        link = f'PR = {PCc} + 4; ' if kind == 'icall' else ''
                        lines.append(f'{{ const uint32_t t = {t}; {link}{self.slot(pc + 2)} FLUSH(); s->pc = t; return; }}')
                    elif kind == 'ret':
                        lines.append(f'{{ const uint32_t t = PR; {self.slot(pc + 2)} FLUSH(); s->pc = t; return; }}')
                    elif kind == 'rte':
                        lines.append(f'{{ const uint32_t t = SPC; set_sr(SSR); {self.slot(pc + 2)} FLUSH(); s->pc = t; return; }}')
                    elif kind == 'trap':
                        count -= 1
                        lines.append(f'FLUSH(); s->trap = {op & 0xFF} + 1; s->pc = {PCc} + 2; return;')
                    elif kind == 'sleep':
                        count -= 1
                        lines.append(f'FLUSH(); s->sleeping = 1; s->pc = {PCc} + 2; return;')
                break
            out.append(f'L_{b:08X}: if (!s->acct) s->cycles += {count};')
            out += ['    ' + l for l in lines]
        out.append('}')
        return '\n'.join(out)


HEADER = '''/* GENERATED from the user's own ROM by core/sh3/gen_c.py. Do not commit, do not distribute. */
#include "sh3.h"
#define S (s)
#include "sh3_ops.h"
void sh3_miss(SH3 *s);
void sh3_miss_at(SH3 *s, uint32_t pc);
void sh3_bad_slot(SH3 *s, uint32_t pc);
#define ACC(n) (s->acc += (n))
#define FLUSH() do { if (s->acct) { s->cycles += s->acc; } s->acc = 0; } while (0)
#define DROP() (s->acc = 0)
#define SH3_FETCH(op, pc) do { if (s->fetch_hook) s->cycles += s->fetch_hook(s->bus.ctx, (pc), (op)); } while (0)
'''


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    per = int(sys.argv[sys.argv.index('--per-file') + 1]) if '--per-file' in sys.argv else 150
    ram = open(args[0], 'rb').read()
    disc = json.load(open(args[1]))
    outdir = args[2]
    os.makedirs(outdir, exist_ok=True)
    g = Gen(ram)
    funcs = sorted((int(k, 16), [int(b, 16) for b in v]) for k, v in disc['functions'].items())
    entries = []  # (block address, function name)
    for fi in range(0, len(funcs), per):
        chunk = funcs[fi:fi + per]
        with open(os.path.join(outdir, f'recomp_{fi // per:03d}.c'), 'w') as fh:
            fh.write(HEADER)
            for e, blocks in chunk:
                fh.write(g.function(e, blocks) + '\n')
                entries += [(b, f'f_{e:08X}') for b in blocks]
    # one dispatch entry per address: prefer the function whose entry it is
    best = {}
    names = {f'f_{e:08X}' for e, _ in funcs}
    for b, fn in entries:
        if b not in best or fn == f'f_{b:08X}':
            best[b] = fn
    with open(os.path.join(outdir, 'recomp_table.c'), 'w') as fh:
        fh.write(HEADER)
        fh.write(''.join(f'void {n}(SH3 *s);\n' for n in sorted(names)))
        fh.write('const struct { uint32_t addr; void (*fn)(SH3 *); } recomp_table[] = {\n')
        fh.write(''.join(f'    {{0x{a:08X}u, {fn}}},\n' for a, fn in sorted(best.items())))
        fh.write('};\nconst unsigned recomp_table_count = sizeof recomp_table / sizeof recomp_table[0];\n')
    print(f'{len(funcs)} functions, {len(best)} dispatch entries, {(len(funcs) + per - 1) // per} files -> {outdir}')


if __name__ == '__main__':
    main()
