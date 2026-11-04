#!/usr/bin/env python3
"""Quick SH-3 disassembler for CV1000 program ROMs (dev tool, needs `pip install capstone`).

Usage: shdis.py <u4 file> <hex offset> <count> [--raw-be]
The u4 dump is stored byte-swapped per 16-bit word (MAME ROM_LOAD16_WORD_SWAP);
it is swapped to big-endian here unless --raw-be is given. Addresses are shown
in the reset area (0xA0000000 + offset). PC-relative literals are annotated.
"""
import struct, sys
import capstone

def load(path, raw_be):
    d = bytearray(open(path, 'rb').read())
    if not raw_be:
        d[0::2], d[1::2] = d[1::2], d[0::2]
    return bytes(d)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    d = load(args[0], '--raw-be' in sys.argv)
    off, n = int(args[1], 16), int(args[2])
    md = capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_SH3)
    for i in md.disasm(d[off:off + n * 2], 0xA0000000 + off):
        note = ''
        if i.mnemonic in ('mov.l', 'mov.w') and i.op_str.startswith('0x'):
            a = int(i.op_str.split(',')[0], 16) & 0x1fffffff
            if a + 4 <= len(d):
                v = struct.unpack('>I', d[a:a + 4])[0] if i.mnemonic == 'mov.l' else struct.unpack('>h', d[a:a + 2])[0] & 0xffff
                note = f'   ; = 0x{v:08x}'
        print(f'{i.address:08x}  {i.mnemonic:<8} {i.op_str}{note}')

if __name__ == '__main__':
    main()
