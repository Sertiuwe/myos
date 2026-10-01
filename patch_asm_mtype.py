import pathlib
p = pathlib.Path("epc_asm.py")
src = p.read_text()

old = '''    elif fmt == 'M':
        # LDL Rd, Rs, off    или    LDL Rd, [Rs+off]
        rd = _parse_reg(A(0))
        rs_tok = A(1)
        off = _parse_imm(A(2)) if len(args) > 2 else 0
        # если есть скобки [Rs+off]
        mb = re.match(r'\\[\\s*[Rr](\\d+)\\s*(?:([+-])\\s*(\\d+))?\\s*\\]', rs_tok)
        if mb:
            rs = int(mb.group(1))
            if mb.group(2):
                sgn = 1 if mb.group(2) == '+' else -1
                off = sgn * int(mb.group(3))
        else:
            rs = _parse_reg(rs_tok)
        if op in ('LDL','LDB','LDH'):
            getattr(e, op.lower())(rd, rs, off)
        else:
            # STL [Rs+off], Rd : у нас stl(self, rs, rd, off)
            getattr(e, op.lower())(rs, rd, off)'''

new = '''    elif fmt == 'M':
        mem_re = re.compile(r'\\[\\s*[Rr](\\d+)\\s*(?:([+-])\\s*(\\d+))?\\s*\\]')
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
            getattr(e, op.lower())(rd, rs, off)'''

if old not in src:
    print("[!] M-type блок не найден")
    raise SystemExit(1)
src = src.replace(old, new, 1)
print("[+] M-type: разбор load/store форм")

# Дизасм — исправить отображение STL/STB/STH
old_d = """        elif f == 'M':
            s = f'{name} R{rd}, [R{rs}+{soff16}]'"""
new_d = """        elif f == 'M':
            if name in ('STL','STB','STH'):
                s = f'{name} [R{rs}+{soff16}], R{rd}'
            else:
                s = f'{name} R{rd}, [R{rs}+{soff16}]'"""
if old_d in src:
    src = src.replace(old_d, new_d, 1)
    print("[+] disasm STx порядок операндов")

p.write_text(src)
print("OK")
