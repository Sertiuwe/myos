#!/usr/bin/env python3
"""epc_asm.py — EP32cm assembler + emitter library.

Использование как библиотеки:
    from epc_asm import Emitter
    e = Emitter()
    e.movi(0, 42)
    e.addi(0, 8)
    e.hlt()
    print(e.hex())     # '1000002A1100000843000000'

Использование как ассемблера:
    python3 epc_asm.py input.s output.hex
    python3 epc_asm.py -d 1000002A11000008      # дизассемблер
"""

import sys, re, struct


# ============================================================
# Операнды и opcodes
# ============================================================
OPS = {
    # int R-type
    'NOP': 0x00, 'MOV': 0x01, 'ADD': 0x02, 'SUB': 0x03,
    'AND': 0x04, 'OR':  0x05, 'XOR': 0x06,
    'SHL': 0x07, 'SHR': 0x08, 'MUL': 0x09, 'DIV': 0x0A,
    'MOD': 0x0B, 'NEG': 0x0C, 'NOT': 0x0D, 'CMP': 0x0E,
    # int I-type
    'MOVI': 0x10, 'ADDI': 0x11, 'SUBI': 0x12,
    'ANDI': 0x13, 'ORI':  0x14, 'XORI': 0x15,
    'SHLI': 0x16, 'SHRI': 0x17, 'MULI': 0x18, 'CMPI': 0x19,
    # int M-type
    'LDL': 0x20, 'LDB': 0x21, 'LDH': 0x22,
    'STL': 0x23, 'STB': 0x24, 'STH': 0x25,
    # B-type
    'JMP': 0x30, 'JZ': 0x31, 'JNZ': 0x32,
    'JC':  0x33, 'JNC': 0x34,
    'CALL': 0x35, 'RET': 0x36,
    'JLT': 0x37, 'JGE': 0x38, 'JGT': 0x39, 'JLE': 0x3A,
    # S-type
    'IN':  0x40, 'OUT': 0x41, 'IRQ': 0x42, 'HLT': 0x43,
    'SEI': 0x44, 'CLI': 0x45, 'RES': 0x46, 'YIELD': 0x47,
    # FPU
    'FADD': 0x50, 'FSUB': 0x51, 'FMUL': 0x52, 'FDIV': 0x53,
    'FSQRT': 0x54, 'FABS': 0x55, 'FNEG': 0x56, 'FMOV': 0x57,
    'FCMP': 0x58, 'FCVTI': 0x59, 'FCVTF': 0x5A,
    'FLDL': 0x5B, 'FSTL': 0x5C,
    'FLDI': 0x5D, 'FLDZ': 0x5E, 'FLD1': 0x5F,
    'FSIN': 0x60, 'FCOS': 0x61, 'FTAN': 0x62,
    'FASIN': 0x63, 'FACOS': 0x64, 'FATAN': 0x65,
    'FATAN2': 0x66,
    'FEXP': 0x67, 'FLOG': 0x68, 'FPOW': 0x69,
}

# Формат для каждой инструкции:
# 'R' = [op:8][rd:4][rs:4][rt:4]
# 'I' = [op:8][rd:4][imm20]
# 'M' = [op:8][rd:4][rs:4][off16]
# 'B' = [op:8][pad:4][off20]
# 'S' = [op:8][rd:4][port:4]
# 'FR' = [op:8][fd:4][fs:4][ft:4]
# 'FI' = [op:8][fd:4][imm20]
# 'FM' = [op:8][fd:4][rs:4][off16]
# 'N' = без операндов
FORMATS = {
    'NOP':'N',
    'MOV':'R','ADD':'R','SUB':'R','AND':'R','OR':'R','XOR':'R',
    'SHL':'R','SHR':'R','MUL':'R','DIV':'R','MOD':'R',
    'NEG':'R1','NOT':'R1','CMP':'R2',
    'MOVI':'I','ADDI':'I','SUBI':'I','ANDI':'I','ORI':'I','XORI':'I',
    'SHLI':'I','SHRI':'I','MULI':'I','CMPI':'I1',
    'LDL':'M','LDB':'M','LDH':'M','STL':'M','STB':'M','STH':'M',
    'JMP':'B','JZ':'B','JNZ':'B','JC':'B','JNC':'B',
    'CALL':'B','RET':'N',
    'JLT':'B','JGE':'B','JGT':'B','JLE':'B',
    'IN':'S','OUT':'S','IRQ':'S',
    'HLT':'N','SEI':'N','CLI':'N','RES':'N','YIELD':'N',
    'FADD':'FR','FSUB':'FR','FMUL':'FR','FDIV':'FR',
    'FSQRT':'FR1','FABS':'FR1','FNEG':'FR1','FMOV':'FR1',
    'FCMP':'FR2',
    'FCVTI':'FR1','FCVTF':'FR1',
    'FLDL':'FM','FSTL':'FM',
    'FLDI':'FI','FLDZ':'F1','FLD1':'F1',
    'FSIN':'FR1','FCOS':'FR1','FTAN':'FR1',
    'FASIN':'FR1','FACOS':'FR1','FATAN':'FR1',
    'FATAN2':'FR','FPOW':'FR',
    'FEXP':'FR1','FLOG':'FR1',
}


# ============================================================
# Emitter
# ============================================================
class Emitter:
    """Низкоуровневый эмиттер инструкций EP32cm."""

    def __init__(self):
        self.words = []          # список 32-битных слов
        self.labels = {}         # name -> word_index
        self.pending = []        # (word_index, label_name) для резолва

    def _e(self, w):
        self.words.append(w & 0xFFFFFFFF)

    def label(self, name):
        self.labels[name] = len(self.words)
        return self

    def _branch(self, op, target):
        """target = int (word offset) или str (label name)."""
        if isinstance(target, str):
            self._e((op << 24) | 0)     # placeholder, оффсет будет позже
            self.pending.append((len(self.words) - 1, target))
        else:
            off = target & 0xFFFFF
            self._e((op << 24) | off)

    def resolve(self):
        """Разрешить отложенные ветвления (вызвать перед hex())."""
        for idx, name in self.pending:
            if name not in self.labels:
                raise ValueError(f"undefined label: {name}")
            target = self.labels[name]
            delta  = target - (idx + 1)     # в словах, относительный
            off    = delta & 0xFFFFF        # signed 20-bit
            op     = (self.words[idx] >> 24) & 0xFF
            self.words[idx] = (op << 24) | off
        self.pending.clear()

    def hex(self):
        self.resolve()
        return ''.join(f'{w:08X}' for w in self.words)

    def bytes_le(self):
        """Для прямой записи в память эмулятора (little-endian)."""
        out = bytearray()
        for w in self.words:
            out += struct.pack('<I', w)
        return bytes(out)

    # ---------- int R-type ----------
    def _R(self, op, rd, rs, rt=0):
        self._e((op << 24) | ((rd&0xF) << 20) | ((rs&0xF) << 16) | ((rt&0xF) << 12))
    def _R1(self, op, rd, rs):
        self._R(op, rd, rs, 0)
    def _R2(self, op, rd, rs):
        self._R(op, rd, rs, 0)

    def nop(self):           self._e(0x00000000)
    def mov(self, rd, rs):   self._R(0x01, rd, rs, 0)
    def add(self, rd, rs, rt): self._R(0x02, rd, rs, rt)
    def sub(self, rd, rs, rt): self._R(0x03, rd, rs, rt)
    def and_(self, rd, rs, rt): self._R(0x04, rd, rs, rt)
    def or_(self, rd, rs, rt):  self._R(0x05, rd, rs, rt)
    def xor(self, rd, rs, rt):  self._R(0x06, rd, rs, rt)
    def shl(self, rd, rs, rt):  self._R(0x07, rd, rs, rt)
    def shr(self, rd, rs, rt):  self._R(0x08, rd, rs, rt)
    def mul(self, rd, rs, rt):  self._R(0x09, rd, rs, rt)
    def div(self, rd, rs, rt):  self._R(0x0A, rd, rs, rt)
    def mod(self, rd, rs, rt):  self._R(0x0B, rd, rs, rt)
    def neg(self, rd, rs):      self._R(0x0C, rd, rs, 0)
    def not_(self, rd, rs):     self._R(0x0D, rd, rs, 0)
    def cmp(self, rs, rt):      self._R(0x0E, 0, rs, rt)

    # ---------- int I-type ----------
    def _I(self, op, rd, imm20):
        self._e((op << 24) | ((rd&0xF) << 20) | (imm20 & 0xFFFFF))
    def movi(self, rd, imm): self._I(0x10, rd, imm)
    def addi(self, rd, imm): self._I(0x11, rd, imm)
    def subi(self, rd, imm): self._I(0x12, rd, imm)
    def andi(self, rd, imm): self._I(0x13, rd, imm)
    def ori(self, rd, imm):  self._I(0x14, rd, imm)
    def xori(self, rd, imm): self._I(0x15, rd, imm)
    def shli(self, rd, imm): self._I(0x16, rd, imm)
    def shri(self, rd, imm): self._I(0x17, rd, imm)
    def muli(self, rd, imm): self._I(0x18, rd, imm)
    def cmpi(self, rd, imm): self._I(0x19, rd, imm)

    # ---------- int M-type ----------
    def _M(self, op, rd, rs, off):
        self._e((op << 24) | ((rd&0xF) << 20) | ((rs&0xF) << 16) | (off & 0xFFFF))
    def ldl(self, rd, rs, off=0): self._M(0x20, rd, rs, off)
    def ldb(self, rd, rs, off=0): self._M(0x21, rd, rs, off)
    def ldh(self, rd, rs, off=0): self._M(0x22, rd, rs, off)
    def stl(self, rs, rd, off=0): self._M(0x23, rd, rs, off)  # STL [Rs+off], Rd
    def stb(self, rs, rd, off=0): self._M(0x24, rd, rs, off)
    def sth(self, rs, rd, off=0): self._M(0x25, rd, rs, off)

    # ---------- branches ----------
    def jmp(self, tgt):  self._branch(0x30, tgt)
    def jz(self, tgt):   self._branch(0x31, tgt)
    def jnz(self, tgt):  self._branch(0x32, tgt)
    def jc(self, tgt):   self._branch(0x33, tgt)
    def jnc(self, tgt):  self._branch(0x34, tgt)
    def call(self, tgt): self._branch(0x35, tgt)
    def ret(self):       self._e(0x36000000)
    def jlt(self, tgt):  self._branch(0x37, tgt)
    def jge(self, tgt):  self._branch(0x38, tgt)
    def jgt(self, tgt):  self._branch(0x39, tgt)
    def jle(self, tgt):  self._branch(0x3A, tgt)

    # ---------- S-type ----------
    def _S(self, op, rd, port):
        self._e((op << 24) | ((rd&0xF) << 20) | ((port&0xF) << 16))
    def in_(self, rd, port):  self._S(0x40, rd, port)
    def out(self, port, rd):  self._S(0x41, rd, port)
    def irq(self, port):      self._S(0x42, 0, port)
    def hlt(self):            self._e(0x43000000)
    def sei(self):            self._e(0x44000000)
    def cli(self):            self._e(0x45000000)
    def res(self):            self._e(0x46000000)
    def yield_(self):         self._e(0x47000000)

    # ---------- FPU ----------
    def _FR(self, op, fd, fs, ft=0):
        self._e((op << 24) | ((fd&0xF) << 20) | ((fs&0xF) << 16) | ((ft&0xF) << 12))
    def fadd(self, fd, fs, ft): self._FR(0x50, fd, fs, ft)
    def fsub(self, fd, fs, ft): self._FR(0x51, fd, fs, ft)
    def fmul(self, fd, fs, ft): self._FR(0x52, fd, fs, ft)
    def fdiv(self, fd, fs, ft): self._FR(0x53, fd, fs, ft)
    def fsqrt(self, fd, fs):    self._FR(0x54, fd, fs, 0)
    def fabs(self, fd, fs):     self._FR(0x55, fd, fs, 0)
    def fneg(self, fd, fs):     self._FR(0x56, fd, fs, 0)
    def fmov(self, fd, fs):     self._FR(0x57, fd, fs, 0)
    def fcmp(self, fs, ft):     self._FR(0x58, 0, fs, ft)
    def fcvti(self, fd, rs):    self._FR(0x59, fd, rs, 0)
    def fcvtf(self, rd, fs):    self._FR(0x5A, rd, fs, 0)
    def fldl(self, fd, rs, off=0):
        self._e((0x5B << 24) | ((fd&0xF) << 20) | ((rs&0xF) << 16) | (off & 0xFFFF))
    def fstl(self, rs, fd, off=0):
        self._e((0x5C << 24) | ((fd&0xF) << 20) | ((rs&0xF) << 16) | (off & 0xFFFF))
    def fldi(self, fd, imm20):
        self._e((0x5D << 24) | ((fd&0xF) << 20) | (imm20 & 0xFFFFF))
    def fldi_float(self, fd, value):
        """Загрузить float через целую часть (imm20 signed). Для 3.14 → ошибка,
        для целых и полуцелых работает через последующее деление."""
        i = int(value)
        self.fldi(fd, i & 0xFFFFF)
    def fldz(self, fd): self._e((0x5E << 24) | ((fd&0xF) << 20))
    def fld1(self, fd): self._e((0x5F << 24) | ((fd&0xF) << 20))

    # --- Трансцендентные (v0.4) ---
    def _FU(self, op, fd, fs):
        self._e((op << 24) | ((fd&0xF) << 20) | ((fs&0xF) << 16))
    def fsin(self, fd, fs):  self._FU(0x60, fd, fs)
    def fcos(self, fd, fs):  self._FU(0x61, fd, fs)
    def ftan(self, fd, fs):  self._FU(0x62, fd, fs)
    def fasin(self, fd, fs): self._FU(0x63, fd, fs)
    def facos(self, fd, fs): self._FU(0x64, fd, fs)
    def fatan(self, fd, fs): self._FU(0x65, fd, fs)
    def fexp(self, fd, fs):  self._FU(0x67, fd, fs)
    def flog(self, fd, fs):  self._FU(0x68, fd, fs)
    def _FB2(self, op, fd, fs, ft):
        self._e((op << 24) | ((fd&0xF) << 20) | ((fs&0xF) << 16) | ((ft&0xF) << 12))
    def fatan2(self, fd, fs, ft): self._FB2(0x66, fd, fs, ft)
    def fpow(self, fd, fs, ft):   self._FB2(0x69, fd, fs, ft)


# ============================================================
# Парсер ассемблера (двухпроходный)
# ============================================================
def _parse_reg(tok):
    m = re.match(r'^[Rr](\d+)$', tok)
    if m:
        n = int(m.group(1))
        if not 0 <= n <= 7: raise ValueError(f"int reg out of range: {tok}")
        return n
    m = re.match(r'^[Ff](\d+)$', tok)
    if m:
        n = int(m.group(1))
        if not 0 <= n <= 7: raise ValueError(f"float reg out of range: {tok}")
        return n
    raise ValueError(f"bad register: {tok}")

def _parse_imm(tok):
    tok = tok.strip()
    if tok.startswith('0x') or tok.startswith('0X'):
        return int(tok, 16)
    if tok.startswith('-'):
        return -int(tok[1:])
    return int(tok)

def _tokenize(line):
    line = re.sub(r';.*$', '', line)        # убрать комментарий
    line = re.sub(r'//.*$', '', line)
    line = line.strip()
    if not line: return None
    # метка в начале
    label = None
    m = re.match(r'^([A-Za-z_]\w*)\s*:\s*(.*)$', line)
    if m:
        label = m.group(1)
        line = m.group(2).strip()
        if not line:
            return ('label', label, None)
    # разбить на opcode и операнды
    m = re.match(r'^(\w+)\s*(.*)$', line)
    if not m:
        return ('label', label, None) if label else None
    op = m.group(1).upper()
    rest = m.group(2)
    args = []
    if rest:
        for a in rest.split(','):
            args.append(a.strip())
    return ('instr', op, args) if not label else ('label_instr', label, (op, args))


def _emit_instr(e, op, args):
    if op not in OPS:
        raise ValueError(f"unknown opcode: {op}")
    fmt = FORMATS[op]

    def A(i):
        if i >= len(args):
            raise ValueError(f"{op}: not enough operands")
        return args[i]

    if fmt == 'N':
        getattr(e, op.lower())()
    elif fmt == 'R':
        rd, rs, rt = _parse_reg(A(0)), _parse_reg(A(1)), _parse_reg(A(2))
        getattr(e, op.lower())(rd, rs, rt)
    elif fmt == 'R1':
        rd, rs = _parse_reg(A(0)), _parse_reg(A(1))
        # для NEG/NOT/CMP с одним операндом — собираем вручную
        if op == 'NEG': e.neg(rd, rs)
        elif op == 'NOT': e.not_(rd, rs)
        else: raise ValueError(op)
    elif fmt == 'R2':
        rs, rt = _parse_reg(A(0)), _parse_reg(A(1))
        if op == 'CMP': e.cmp(rs, rt)
        else: raise ValueError(op)
    elif fmt == 'I':
        rd, imm = _parse_reg(A(0)), _parse_imm(A(1))
        getattr(e, op.lower())(rd, imm)
    elif fmt == 'I1':
        rd, imm = _parse_reg(A(0)), _parse_imm(A(1))
        if op == 'CMPI': e.cmpi(rd, imm)
        else: raise ValueError(op)
    elif fmt == 'M':
        mem_re = re.compile(r'\[\s*[Rr](\d+)\s*(?:([+-])\s*(\d+))?\s*\]')
        if A(0).startswith('['):
            # Store form: STx [Rs+off], Rd
            rs_tok = A(0)
            rd = _parse_reg(A(1))
            mb = mem_re.match(rs_tok)
            if not mb:
                raise ValueError(f'bad memory operand: {rs_tok}')
            rs = int(mb.group(1))
            off = 0
            if mb.group(2):
                sgn = 1 if mb.group(2) == '+' else -1
                off = sgn * int(mb.group(3))
            getattr(e, op.lower())(rs, rd, off)
        else:
            # Load form: LDx Rd, [Rs+off]
            rd = _parse_reg(A(0))
            rs_tok = A(1)
            off = _parse_imm(A(2)) if len(args) > 2 else 0
            mb = mem_re.match(rs_tok)
            if mb:
                rs = int(mb.group(1))
                if mb.group(2):
                    sgn = 1 if mb.group(2) == '+' else -1
                    off = sgn * int(mb.group(3))
            else:
                rs = _parse_reg(rs_tok)
            getattr(e, op.lower())(rd, rs, off)
    elif fmt == 'B':
        tgt = A(0)
        if re.match(r'^-?\d+$', tgt):
            getattr(e, op.lower())(_parse_imm(tgt))
        else:
            getattr(e, op.lower())(tgt)          # label
    elif fmt == 'S':
        if op == 'IN':
            rd, port = _parse_reg(A(0)), _parse_imm(A(1))
            e.in_(rd, port)
        elif op == 'OUT':
            port, rd = _parse_imm(A(0)), _parse_reg(A(1))
            e.out(port, rd)
        elif op == 'IRQ':
            e.irq(_parse_imm(A(0)))
        else:
            raise ValueError(op)
    elif fmt == 'FR':
        fd, fs, ft = _parse_reg(A(0)), _parse_reg(A(1)), _parse_reg(A(2))
        getattr(e, op.lower())(fd, fs, ft)
    elif fmt == 'FR1':
        fd, fs = _parse_reg(A(0)), _parse_reg(A(1))
        if op in ('FSQRT','FABS','FNEG','FMOV',
                  'FSIN','FCOS','FTAN','FASIN','FACOS','FATAN','FEXP','FLOG'):
            getattr(e, op.lower())(fd, fs)
        elif op == 'FCVTI':
            e.fcvti(fd, fs)
        elif op == 'FCVTF':
            e.fcvtf(fd, fs)
        else:
            raise ValueError(op)
    elif fmt == 'FR2':
        fs, ft = _parse_reg(A(0)), _parse_reg(A(1))
        if op == 'FCMP': e.fcmp(fs, ft)
        else: raise ValueError(op)
    elif fmt == 'FM':
        fd = _parse_reg(A(0))
        rs_tok = A(1)
        off = _parse_imm(A(2)) if len(args) > 2 else 0
        mb = re.match(r'\[\s*[Rr](\d+)\s*(?:([+-])\s*(\d+))?\s*\]', rs_tok)
        if mb:
            rs = int(mb.group(1))
            if mb.group(2):
                sgn = 1 if mb.group(2) == '+' else -1
                off = sgn * int(mb.group(3))
        else:
            rs = _parse_reg(rs_tok)
        if op == 'FLDL': e.fldl(fd, rs, off)
        else: e.fstl(rs, fd, off)
    elif fmt == 'FI':
        fd, imm = _parse_reg(A(0)), _parse_imm(A(1))
        if op == 'FLDI': e.fldi(fd, imm)
        else: raise ValueError(op)
    elif fmt == 'F1':
        fd = _parse_reg(A(0))
        if op == 'FLDZ': e.fldz(fd)
        elif op == 'FLD1': e.fld1(fd)
        else: raise ValueError(op)
    else:
        raise ValueError(f"fmt {fmt} not implemented for {op}")


def assemble(text):
    e = Emitter()
    for raw in text.splitlines():
        t = _tokenize(raw)
        if t is None: continue
        kind = t[0]
        if kind == 'label':
            e.label(t[1])
        elif kind == 'instr':
            _emit_instr(e, t[1], t[2])
        elif kind == 'label_instr':
            e.label(t[1])
            _emit_instr(e, t[2][0], t[2][1])
    e.resolve()
    return e.hex()


# ============================================================
# Дизассемблер (для отладки)
# ============================================================
OP_NAMES = {v: k for k, v in OPS.items()}

def disasm(hex_str):
    hex_str = hex_str.replace(' ', '').replace('\n', '')
    out_lines = []
    for i in range(0, len(hex_str), 8):
        w = int(hex_str[i:i+8], 16)
        op = (w >> 24) & 0xFF
        name = OP_NAMES.get(op, f'UNK{op:02X}')
        rd = (w >> 20) & 0xF
        rs = (w >> 16) & 0xF
        rt = (w >> 12) & 0xF
        imm20 = w & 0xFFFFF
        simm20 = imm20 - 0x100000 if imm20 & 0x80000 else imm20
        off16 = w & 0xFFFF
        soff16 = off16 - 0x10000 if off16 & 0x8000 else off16
        f = FORMATS.get(name, '?')
        if f == 'N':
            s = f'{name}'
        elif f == 'R':
            s = f'{name} R{rd}, R{rs}, R{rt}'
        elif f == 'R1':
            s = f'{name} R{rd}, R{rs}'
        elif f == 'R2':
            s = f'{name} R{rs}, R{rt}'
        elif f == 'I':
            s = f'{name} R{rd}, {simm20}'
        elif f == 'I1':
            s = f'{name} R{rd}, {simm20}'
        elif f == 'M':
            if name in ('STL','STB','STH'):
                s = f'{name} [R{rs}+{soff16}], R{rd}'
            else:
                s = f'{name} R{rd}, [R{rs}+{soff16}]'
        elif f == 'B':
            s = f'{name} {simm20:+d}'
        elif f == 'S':
            if name == 'IN':  s = f'IN R{rd}, {rs}'
            elif name == 'OUT': s = f'OUT {rs}, R{rd}'
            elif name == 'IRQ': s = f'IRQ {rd}'
            else: s = name
        elif f == 'FR':
            s = f'{name} F{rd}, F{rs}, F{rt}'
        elif f == 'FR1':
            if name == 'FCVTF':
                s = f'{name} R{rd}, F{rs}'
            elif name == 'FCVTI':
                s = f'{name} F{rd}, R{rs}'
            else:
                s = f'{name} F{rd}, F{rs}'
        elif f == 'FR2':
            s = f'{name} F{rs}, F{rt}'
        elif f == 'FM':
            s = f'{name} F{rd}, [R{rs}+{soff16}]'
        elif f == 'FI':
            s = f'{name} F{rd}, {simm20}'
        elif f == 'F1':
            s = f'{name} F{rd}'
        else:
            s = f'{name} 0x{w:08X}'
        out_lines.append(f'{i//2:04X}: {w:08X}  {s}')
    return '\n'.join(out_lines)


# ============================================================
# CLI
# ============================================================
def main():
    if len(sys.argv) >= 3 and sys.argv[1] == '-d':
        print(disasm(sys.argv[2]))
        return
    if len(sys.argv) >= 3:
        src = open(sys.argv[1]).read()
        hex_out = assemble(src)
        open(sys.argv[2], 'w').write(hex_out + '\n')
        n = len(hex_out) // 8
        print(f"OK: {n} words -> {sys.argv[2]}")
        return
    print("usage:")
    print("  python3 epc_asm.py input.s output.hex")
    print("  python3 epc_asm.py -d <hex>")


if __name__ == '__main__':
    main()
