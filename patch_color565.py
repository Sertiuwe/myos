import pathlib
p = pathlib.Path("epcc.py")
src = p.read_text()

old = '''        if name == 'color565':
            # color565(r,g,b) -> в R0
            self.gen(args[0])              # r
            self.e.movi(1, 3)
            self.e.shl(0, 0, 1)            # r << 3
            self.push()
            self.gen(args[1])              # g
            self.e.movi(1, 2)
            self.e.shl(0, 0, 1)            # g << 2
            self.pop(1)
            self.e.or_(0, 1, 0)
            self.push()
            self.gen(args[2])              # b
            self.e.movi(1, 3)
            self.e.shr(0, 0, 1)            # b >> 3
            self.pop(1)
            self.e.or_(0, 1, 0)
            return'''

new = '''        if name == 'color565':
            # color565(r5,g6,b5) -> RGB565 в R0
            # r 0..31 -> r<<11, g 0..63 -> g<<5, b 0..31 -> b
            self.gen(args[0])              # r
            self.e.movi(1, 11)
            self.e.shl(0, 0, 1)            # r << 11
            self.push()
            self.gen(args[1])              # g
            self.e.movi(1, 5)
            self.e.shl(0, 0, 1)            # g << 5
            self.pop(1)
            self.e.or_(0, 1, 0)
            self.push()
            self.gen(args[2])              # b
            self.pop(1)
            self.e.or_(0, 1, 0)
            return'''

if old not in src:
    print("[!] color565 не найден")
    raise SystemExit(1)
src = src.replace(old, new, 1)
p.write_text(src)
print("[+] color565: сдвиги 11/5/0")
