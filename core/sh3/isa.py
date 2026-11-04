"""SH-3 (SH7709S, no FPU) instruction table: the single source of truth for decode and semantics.

Each entry: (pattern, mnemonic, kind, C body)
  pattern: 16 chars, '0'/'1' fixed bits; n, m = register fields; i = 8-bit immediate;
           d = displacement (4, 8 or 12 bits).
  kind:    'op' normal | 'cbr' conditional branch, no delay slot | 'cbrs' conditional, delay slot |
           'br' unconditional delayed | 'ijmp' indirect delayed (target known only at run time) |
           'call' delayed call (pc-relative) | 'icall' delayed indirect call | 'ret' rts | 'rte' | 'trap' | 'sleep'
  body:    C code. Available names (from sh3_ops.h):
           Rn, Rm, R0 (lvalues), n, m, IMM (u8), SIMM (s8), D4, D8, D12 (raw), PC (address of this insn),
           T (read), SET_T(x), SR/GBR/VBR/SSR/SPC/MACH/MACL/PR (lvalues), set_sr(v),
           RB/RW/RL(addr) (zero-extended reads), WB/WW/WL(addr, v), DELAY(target), JUMP(target),
           TRAPA(imm), SLEEP().
Branch targets for 'cbr'/'cbrs'/'br'/'call' are static: tgt_* functions below compute them.
"""

def s8(x): return x - 256 if x & 0x80 else x
def s12(x): return x - 4096 if x & 0x800 else x

T8 = 'PC + 4 + ((int8_t)D8 << 1)'
T12 = 'PC + 4 + ((int32_t)((D12 ^ 0x800) - 0x800) << 1)'

ISA = [
    # ---- data transfer ----
    ('1110nnnniiiiiiii', 'mov #imm,Rn', 'op', 'Rn = (uint32_t)(int32_t)SIMM;'),
    ('1001nnnndddddddd', 'mov.w @(disp,PC),Rn', 'op', 'Rn = (uint32_t)(int16_t)RW(PC + 4 + D8 * 2);'),
    ('1101nnnndddddddd', 'mov.l @(disp,PC),Rn', 'op', 'Rn = RL(((PC + 4) & ~3u) + D8 * 4);'),
    ('0110nnnnmmmm0011', 'mov Rm,Rn', 'op', 'Rn = Rm;'),
    ('0010nnnnmmmm0000', 'mov.b Rm,@Rn', 'op', 'WB(Rn, Rm);'),
    ('0010nnnnmmmm0001', 'mov.w Rm,@Rn', 'op', 'WW(Rn, Rm);'),
    ('0010nnnnmmmm0010', 'mov.l Rm,@Rn', 'op', 'WL(Rn, Rm);'),
    ('0110nnnnmmmm0000', 'mov.b @Rm,Rn', 'op', 'Rn = (uint32_t)(int8_t)RB(Rm);'),
    ('0110nnnnmmmm0001', 'mov.w @Rm,Rn', 'op', 'Rn = (uint32_t)(int16_t)RW(Rm);'),
    ('0110nnnnmmmm0010', 'mov.l @Rm,Rn', 'op', 'Rn = RL(Rm);'),
    ('0010nnnnmmmm0100', 'mov.b Rm,@-Rn', 'op', '{ uint32_t v = Rm; WB(Rn - 1, v); Rn -= 1; }'),
    ('0010nnnnmmmm0101', 'mov.w Rm,@-Rn', 'op', '{ uint32_t v = Rm; WW(Rn - 2, v); Rn -= 2; }'),
    ('0010nnnnmmmm0110', 'mov.l Rm,@-Rn', 'op', '{ uint32_t v = Rm; WL(Rn - 4, v); Rn -= 4; }'),
    ('0110nnnnmmmm0100', 'mov.b @Rm+,Rn', 'op', '{ uint32_t a = Rm; uint32_t v = (uint32_t)(int8_t)RB(a); if (n != m) Rm = a + 1; Rn = v; }'),
    ('0110nnnnmmmm0101', 'mov.w @Rm+,Rn', 'op', '{ uint32_t a = Rm; uint32_t v = (uint32_t)(int16_t)RW(a); if (n != m) Rm = a + 2; Rn = v; }'),
    ('0110nnnnmmmm0110', 'mov.l @Rm+,Rn', 'op', '{ uint32_t a = Rm; uint32_t v = RL(a); if (n != m) Rm = a + 4; Rn = v; }'),
    ('10000000nnnndddd', 'mov.b R0,@(disp,Rn)', 'op', 'WB(Rn + D4, R0);'),
    ('10000001nnnndddd', 'mov.w R0,@(disp,Rn)', 'op', 'WW(Rn + D4 * 2, R0);'),
    ('0001nnnnmmmmdddd', 'mov.l Rm,@(disp,Rn)', 'op', 'WL(Rn + D4 * 4, Rm);'),
    ('10000100mmmmdddd', 'mov.b @(disp,Rm),R0', 'op', 'R0 = (uint32_t)(int8_t)RB(Rm + D4);'),
    ('10000101mmmmdddd', 'mov.w @(disp,Rm),R0', 'op', 'R0 = (uint32_t)(int16_t)RW(Rm + D4 * 2);'),
    ('0101nnnnmmmmdddd', 'mov.l @(disp,Rm),Rn', 'op', 'Rn = RL(Rm + D4 * 4);'),
    ('0000nnnnmmmm0100', 'mov.b Rm,@(R0,Rn)', 'op', 'WB(R0 + Rn, Rm);'),
    ('0000nnnnmmmm0101', 'mov.w Rm,@(R0,Rn)', 'op', 'WW(R0 + Rn, Rm);'),
    ('0000nnnnmmmm0110', 'mov.l Rm,@(R0,Rn)', 'op', 'WL(R0 + Rn, Rm);'),
    ('0000nnnnmmmm1100', 'mov.b @(R0,Rm),Rn', 'op', 'Rn = (uint32_t)(int8_t)RB(R0 + Rm);'),
    ('0000nnnnmmmm1101', 'mov.w @(R0,Rm),Rn', 'op', 'Rn = (uint32_t)(int16_t)RW(R0 + Rm);'),
    ('0000nnnnmmmm1110', 'mov.l @(R0,Rm),Rn', 'op', 'Rn = RL(R0 + Rm);'),
    ('11000000dddddddd', 'mov.b R0,@(disp,GBR)', 'op', 'WB(GBR + D8, R0);'),
    ('11000001dddddddd', 'mov.w R0,@(disp,GBR)', 'op', 'WW(GBR + D8 * 2, R0);'),
    ('11000010dddddddd', 'mov.l R0,@(disp,GBR)', 'op', 'WL(GBR + D8 * 4, R0);'),
    ('11000100dddddddd', 'mov.b @(disp,GBR),R0', 'op', 'R0 = (uint32_t)(int8_t)RB(GBR + D8);'),
    ('11000101dddddddd', 'mov.w @(disp,GBR),R0', 'op', 'R0 = (uint32_t)(int16_t)RW(GBR + D8 * 2);'),
    ('11000110dddddddd', 'mov.l @(disp,GBR),R0', 'op', 'R0 = RL(GBR + D8 * 4);'),
    ('11000111dddddddd', 'mova @(disp,PC),R0', 'op', 'R0 = ((PC + 4) & ~3u) + D8 * 4;'),
    ('0000nnnn00101001', 'movt Rn', 'op', 'Rn = T;'),
    ('0110nnnnmmmm1000', 'swap.b Rm,Rn', 'op', 'Rn = (Rm & 0xffff0000u) | ((Rm & 0xff) << 8) | ((Rm >> 8) & 0xff);'),
    ('0110nnnnmmmm1001', 'swap.w Rm,Rn', 'op', 'Rn = (Rm << 16) | (Rm >> 16);'),
    ('0010nnnnmmmm1101', 'xtrct Rm,Rn', 'op', 'Rn = (Rm << 16) | (Rn >> 16);'),
    # ---- arithmetic ----
    ('0011nnnnmmmm1100', 'add Rm,Rn', 'op', 'Rn += Rm;'),
    ('0111nnnniiiiiiii', 'add #imm,Rn', 'op', 'Rn += (uint32_t)(int32_t)SIMM;'),
    ('0011nnnnmmmm1110', 'addc Rm,Rn', 'op', '{ uint32_t a = Rn, b = Rm, t = T; uint32_t sum = a + b; uint32_t r = sum + t; SET_T((sum < a) | (r < sum)); Rn = r; }'),
    ('0011nnnnmmmm1111', 'addv Rm,Rn', 'op', '{ int32_t a = (int32_t)Rn, b = (int32_t)Rm; int32_t r = (int32_t)((uint32_t)a + (uint32_t)b); SET_T(((a ^ r) & (b ^ r)) < 0); Rn = (uint32_t)r; }'),
    ('10001000iiiiiiii', 'cmp/eq #imm,R0', 'op', 'SET_T(R0 == (uint32_t)(int32_t)SIMM);'),
    ('0011nnnnmmmm0000', 'cmp/eq Rm,Rn', 'op', 'SET_T(Rn == Rm);'),
    ('0011nnnnmmmm0010', 'cmp/hs Rm,Rn', 'op', 'SET_T(Rn >= Rm);'),
    ('0011nnnnmmmm0011', 'cmp/ge Rm,Rn', 'op', 'SET_T((int32_t)Rn >= (int32_t)Rm);'),
    ('0011nnnnmmmm0110', 'cmp/hi Rm,Rn', 'op', 'SET_T(Rn > Rm);'),
    ('0011nnnnmmmm0111', 'cmp/gt Rm,Rn', 'op', 'SET_T((int32_t)Rn > (int32_t)Rm);'),
    ('0100nnnn00010001', 'cmp/pz Rn', 'op', 'SET_T((int32_t)Rn >= 0);'),
    ('0100nnnn00010101', 'cmp/pl Rn', 'op', 'SET_T((int32_t)Rn > 0);'),
    ('0010nnnnmmmm1100', 'cmp/str Rm,Rn', 'op', '{ uint32_t x = Rn ^ Rm; SET_T(!(x & 0xff000000u) || !(x & 0xff0000u) || !(x & 0xff00u) || !(x & 0xffu)); }'),
    ('0011nnnnmmmm0100', 'div1 Rm,Rn', 'op', 'sh3_div1(s, &Rn, Rm);'),
    ('0010nnnnmmmm0111', 'div0s Rm,Rn', 'op', '{ uint32_t q = Rn >> 31, mm = Rm >> 31; SR = (SR & ~(SR_Q | SR_M | 1u)) | (q << 8) | (mm << 9) | (q ^ mm); }'),
    ('0000000000011001', 'div0u', 'op', 'SR &= ~(SR_Q | SR_M | 1u);'),
    ('0011nnnnmmmm1101', 'dmuls.l Rm,Rn', 'op', '{ int64_t r = (int64_t)(int32_t)Rn * (int64_t)(int32_t)Rm; MACH = (uint32_t)((uint64_t)r >> 32); MACL = (uint32_t)r; }'),
    ('0011nnnnmmmm0101', 'dmulu.l Rm,Rn', 'op', '{ uint64_t r = (uint64_t)Rn * Rm; MACH = (uint32_t)(r >> 32); MACL = (uint32_t)r; }'),
    ('0100nnnn00010000', 'dt Rn', 'op', 'Rn -= 1; SET_T(Rn == 0);'),
    ('0110nnnnmmmm1110', 'exts.b Rm,Rn', 'op', 'Rn = (uint32_t)(int8_t)Rm;'),
    ('0110nnnnmmmm1111', 'exts.w Rm,Rn', 'op', 'Rn = (uint32_t)(int16_t)Rm;'),
    ('0110nnnnmmmm1100', 'extu.b Rm,Rn', 'op', 'Rn = Rm & 0xff;'),
    ('0110nnnnmmmm1101', 'extu.w Rm,Rn', 'op', 'Rn = Rm & 0xffff;'),
    ('0000nnnnmmmm1111', 'mac.l @Rm+,@Rn+', 'op', 'sh3_macl(s, n, m);'),
    ('0100nnnnmmmm1111', 'mac.w @Rm+,@Rn+', 'op', 'sh3_macw(s, n, m);'),
    ('0000nnnnmmmm0111', 'mul.l Rm,Rn', 'op', 'MACL = Rn * Rm;'),
    ('0010nnnnmmmm1111', 'muls.w Rm,Rn', 'op', 'MACL = (uint32_t)((int32_t)(int16_t)Rn * (int32_t)(int16_t)Rm);'),
    ('0010nnnnmmmm1110', 'mulu.w Rm,Rn', 'op', 'MACL = (Rn & 0xffff) * (Rm & 0xffff);'),
    ('0110nnnnmmmm1011', 'neg Rm,Rn', 'op', 'Rn = 0u - Rm;'),
    ('0110nnnnmmmm1010', 'negc Rm,Rn', 'op', '{ uint32_t b = Rm, t = T; uint32_t s0 = 0u - b; uint32_t r = s0 - t; SET_T((0u < s0) | (s0 < r)); Rn = r; }'),
    ('0011nnnnmmmm1000', 'sub Rm,Rn', 'op', 'Rn -= Rm;'),
    ('0011nnnnmmmm1010', 'subc Rm,Rn', 'op', '{ uint32_t a = Rn, b = Rm, t = T; uint32_t s0 = a - b; uint32_t r = s0 - t; SET_T((a < s0) | (s0 < r)); Rn = r; }'),
    ('0011nnnnmmmm1011', 'subv Rm,Rn', 'op', '{ int32_t a = (int32_t)Rn, b = (int32_t)Rm; int32_t r = (int32_t)((uint32_t)a - (uint32_t)b); SET_T(((a ^ b) & (a ^ r)) < 0); Rn = (uint32_t)r; }'),
    # ---- logic ----
    ('0010nnnnmmmm1001', 'and Rm,Rn', 'op', 'Rn &= Rm;'),
    ('11001001iiiiiiii', 'and #imm,R0', 'op', 'R0 &= IMM;'),
    ('11001101iiiiiiii', 'and.b #imm,@(R0,GBR)', 'op', '{ uint32_t a = GBR + R0; WB(a, RB(a) & IMM); }'),
    ('0110nnnnmmmm0111', 'not Rm,Rn', 'op', 'Rn = ~Rm;'),
    ('0010nnnnmmmm1011', 'or Rm,Rn', 'op', 'Rn |= Rm;'),
    ('11001011iiiiiiii', 'or #imm,R0', 'op', 'R0 |= IMM;'),
    ('11001111iiiiiiii', 'or.b #imm,@(R0,GBR)', 'op', '{ uint32_t a = GBR + R0; WB(a, RB(a) | IMM); }'),
    ('0100nnnn00011011', 'tas.b @Rn', 'op', '{ uint32_t a = Rn; uint32_t v = RB(a); SET_T(v == 0); WB(a, v | 0x80); }'),
    ('0010nnnnmmmm1000', 'tst Rm,Rn', 'op', 'SET_T((Rn & Rm) == 0);'),
    ('11001000iiiiiiii', 'tst #imm,R0', 'op', 'SET_T((R0 & IMM) == 0);'),
    ('11001100iiiiiiii', 'tst.b #imm,@(R0,GBR)', 'op', 'SET_T((RB(GBR + R0) & IMM) == 0);'),
    ('0010nnnnmmmm1010', 'xor Rm,Rn', 'op', 'Rn ^= Rm;'),
    ('11001010iiiiiiii', 'xor #imm,R0', 'op', 'R0 ^= IMM;'),
    ('11001110iiiiiiii', 'xor.b #imm,@(R0,GBR)', 'op', '{ uint32_t a = GBR + R0; WB(a, RB(a) ^ IMM); }'),
    # ---- shifts ----
    ('0100nnnn00000100', 'rotl Rn', 'op', 'SET_T(Rn >> 31); Rn = (Rn << 1) | (Rn >> 31);'),
    ('0100nnnn00000101', 'rotr Rn', 'op', 'SET_T(Rn & 1); Rn = (Rn >> 1) | (Rn << 31);'),
    ('0100nnnn00100100', 'rotcl Rn', 'op', '{ uint32_t t = T; SET_T(Rn >> 31); Rn = (Rn << 1) | t; }'),
    ('0100nnnn00100101', 'rotcr Rn', 'op', '{ uint32_t t = T; SET_T(Rn & 1); Rn = (Rn >> 1) | (t << 31); }'),
    ('0100nnnnmmmm1100', 'shad Rm,Rn', 'op', '{ int32_t sh = (int32_t)Rm; if (sh >= 0) Rn <<= (sh & 31); else if ((sh & 31) == 0) Rn = ((int32_t)Rn < 0) ? 0xffffffffu : 0; else Rn = (uint32_t)((int32_t)Rn >> ((~sh & 31) + 1)); }'),
    ('0100nnnnmmmm1101', 'shld Rm,Rn', 'op', '{ int32_t sh = (int32_t)Rm; if (sh >= 0) Rn <<= (sh & 31); else if ((sh & 31) == 0) Rn = 0; else Rn >>= ((~sh & 31) + 1); }'),
    ('0100nnnn00100000', 'shal Rn', 'op', 'SET_T(Rn >> 31); Rn <<= 1;'),
    ('0100nnnn00100001', 'shar Rn', 'op', 'SET_T(Rn & 1); Rn = (uint32_t)((int32_t)Rn >> 1);'),
    ('0100nnnn00000000', 'shll Rn', 'op', 'SET_T(Rn >> 31); Rn <<= 1;'),
    ('0100nnnn00000001', 'shlr Rn', 'op', 'SET_T(Rn & 1); Rn >>= 1;'),
    ('0100nnnn00001000', 'shll2 Rn', 'op', 'Rn <<= 2;'),
    ('0100nnnn00001001', 'shlr2 Rn', 'op', 'Rn >>= 2;'),
    ('0100nnnn00011000', 'shll8 Rn', 'op', 'Rn <<= 8;'),
    ('0100nnnn00011001', 'shlr8 Rn', 'op', 'Rn >>= 8;'),
    ('0100nnnn00101000', 'shll16 Rn', 'op', 'Rn <<= 16;'),
    ('0100nnnn00101001', 'shlr16 Rn', 'op', 'Rn >>= 16;'),
    # ---- branches ----
    ('10001011dddddddd', 'bf disp', 'cbr', f'if (!T) JUMP({T8});'),
    ('10001111dddddddd', 'bf/s disp', 'cbrs', f'if (!T) DELAY({T8});'),
    ('10001001dddddddd', 'bt disp', 'cbr', f'if (T) JUMP({T8});'),
    ('10001101dddddddd', 'bt/s disp', 'cbrs', f'if (T) DELAY({T8});'),
    ('1010dddddddddddd', 'bra disp', 'br', f'DELAY({T12});'),
    ('0000nnnn00100011', 'braf Rn', 'ijmp', 'DELAY(PC + 4 + Rn);'),
    ('1011dddddddddddd', 'bsr disp', 'call', f'PR = PC + 4; DELAY({T12});'),
    ('0000nnnn00000011', 'bsrf Rn', 'icall', '{ uint32_t t = PC + 4 + Rn; PR = PC + 4; DELAY(t); }'),
    ('0100nnnn00101011', 'jmp @Rn', 'ijmp', 'DELAY(Rn);'),
    ('0100nnnn00001011', 'jsr @Rn', 'icall', '{ uint32_t t = Rn; PR = PC + 4; DELAY(t); }'),
    ('0000000000001011', 'rts', 'ret', 'DELAY(PR);'),
    # ---- system ----
    ('0000000000101000', 'clrmac', 'op', 'MACH = MACL = 0;'),
    ('0000000001001000', 'clrs', 'op', 'SR &= ~SR_S;'),
    ('0000000000001000', 'clrt', 'op', 'SET_T(0);'),
    ('0000000001011000', 'sets', 'op', 'SR |= SR_S;'),
    ('0000000000011000', 'sett', 'op', 'SET_T(1);'),
    ('0000000000001001', 'nop', 'op', ''),
    ('0000000000111000', 'ldtlb', 'op', '/* MMU unused on CV1000 */'),
    ('0000nnnn10000011', 'pref @Rn', 'op', '/* prefetch: no effect */'),
    ('0000000000101011', 'rte', 'rte', '{ uint32_t t = SPC; set_sr(SSR); DELAY(t); }'),
    ('0000000000011011', 'sleep', 'sleep', 'SLEEP();'),
    ('11000011iiiiiiii', 'trapa #imm', 'trap', 'TRAPA(IMM);'),
    ('0100mmmm00001110', 'ldc Rm,SR', 'op', 'set_sr(Rm);'),
    ('0100mmmm00011110', 'ldc Rm,GBR', 'op', 'GBR = Rm;'),
    ('0100mmmm00101110', 'ldc Rm,VBR', 'op', 'VBR = Rm;'),
    ('0100mmmm00111110', 'ldc Rm,SSR', 'op', 'SSR = Rm;'),
    ('0100mmmm01001110', 'ldc Rm,SPC', 'op', 'SPC = Rm;'),
    ('0100mmmm1nnn1110', 'ldc Rm,Rn_BANK', 'op', 'BANK(n & 7) = Rm;'),
    ('0100mmmm00000111', 'ldc.l @Rm+,SR', 'op', '{ uint32_t v = RL(Rm); Rm += 4; set_sr(v); }'),
    ('0100mmmm00010111', 'ldc.l @Rm+,GBR', 'op', 'GBR = RL(Rm); Rm += 4;'),
    ('0100mmmm00100111', 'ldc.l @Rm+,VBR', 'op', 'VBR = RL(Rm); Rm += 4;'),
    ('0100mmmm00110111', 'ldc.l @Rm+,SSR', 'op', 'SSR = RL(Rm); Rm += 4;'),
    ('0100mmmm01000111', 'ldc.l @Rm+,SPC', 'op', 'SPC = RL(Rm); Rm += 4;'),
    ('0100mmmm1nnn0111', 'ldc.l @Rm+,Rn_BANK', 'op', 'BANK(n & 7) = RL(Rm); Rm += 4;'),
    ('0100mmmm00001010', 'lds Rm,MACH', 'op', 'MACH = Rm;'),
    ('0100mmmm00011010', 'lds Rm,MACL', 'op', 'MACL = Rm;'),
    ('0100mmmm00101010', 'lds Rm,PR', 'op', 'PR = Rm;'),
    ('0100mmmm00000110', 'lds.l @Rm+,MACH', 'op', 'MACH = RL(Rm); Rm += 4;'),
    ('0100mmmm00010110', 'lds.l @Rm+,MACL', 'op', 'MACL = RL(Rm); Rm += 4;'),
    ('0100mmmm00100110', 'lds.l @Rm+,PR', 'op', 'PR = RL(Rm); Rm += 4;'),
    ('0000nnnn00000010', 'stc SR,Rn', 'op', 'Rn = SR;'),
    ('0000nnnn00010010', 'stc GBR,Rn', 'op', 'Rn = GBR;'),
    ('0000nnnn00100010', 'stc VBR,Rn', 'op', 'Rn = VBR;'),
    ('0000nnnn00110010', 'stc SSR,Rn', 'op', 'Rn = SSR;'),
    ('0000nnnn01000010', 'stc SPC,Rn', 'op', 'Rn = SPC;'),
    ('0000nnnn1mmm0010', 'stc Rm_BANK,Rn', 'op', 'Rn = BANK(m & 7);'),
    ('0100nnnn00000011', 'stc.l SR,@-Rn', 'op', 'WL(Rn - 4, SR); Rn -= 4;'),
    ('0100nnnn00010011', 'stc.l GBR,@-Rn', 'op', 'WL(Rn - 4, GBR); Rn -= 4;'),
    ('0100nnnn00100011', 'stc.l VBR,@-Rn', 'op', 'WL(Rn - 4, VBR); Rn -= 4;'),
    ('0100nnnn00110011', 'stc.l SSR,@-Rn', 'op', 'WL(Rn - 4, SSR); Rn -= 4;'),
    ('0100nnnn01000011', 'stc.l SPC,@-Rn', 'op', 'WL(Rn - 4, SPC); Rn -= 4;'),
    ('0100nnnn1mmm0011', 'stc.l Rm_BANK,@-Rn', 'op', 'WL(Rn - 4, BANK(m & 7)); Rn -= 4;'),
    ('0000nnnn00001010', 'sts MACH,Rn', 'op', 'Rn = MACH;'),
    ('0000nnnn00011010', 'sts MACL,Rn', 'op', 'Rn = MACL;'),
    ('0000nnnn00101010', 'sts PR,Rn', 'op', 'Rn = PR;'),
    ('0100nnnn00000010', 'sts.l MACH,@-Rn', 'op', 'WL(Rn - 4, MACH); Rn -= 4;'),
    ('0100nnnn00010010', 'sts.l MACL,@-Rn', 'op', 'WL(Rn - 4, MACL); Rn -= 4;'),
    ('0100nnnn00100010', 'sts.l PR,@-Rn', 'op', 'WL(Rn - 4, PR); Rn -= 4;'),
]


def match(pattern, op):
    for k, ch in enumerate(pattern):
        bit = (op >> (15 - k)) & 1
        if ch == '0' and bit: return False
        if ch == '1' and not bit: return False
    return True


def build_decode_table():
    """op (0..65535) -> index into ISA, or -1. Patterns are checked for overlaps."""
    table = [-1] * 65536
    for idx, (pat, *_rest) in enumerate(ISA):
        fixed = int(''.join('1' if c in '01' else '0' for c in pat), 2)
        val = int(''.join('1' if c == '1' else '0' for c in pat), 2)
        for op in range(65536):
            if op & fixed == val:
                if table[op] != -1:
                    raise ValueError(f'overlap {op:04x}: {ISA[table[op]][1]} / {ISA[idx][1]}')
                table[op] = idx
    return table


def static_target(entry, op, pc):
    """Branch target for pc-relative branches, else None."""
    pat, mn, kind, _ = entry
    if kind in ('cbr', 'cbrs'):
        return (pc + 4 + s8(op & 0xff) * 2) & 0xffffffff
    if kind in ('br', 'call'):
        return (pc + 4 + s12(op & 0xfff) * 2) & 0xffffffff
    return None
