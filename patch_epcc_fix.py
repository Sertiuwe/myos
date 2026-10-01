import pathlib
p = pathlib.Path("epcc.py")
src = p.read_text()
ok = True

def sub(s, old, new, tag):
    global ok
    if old not in s:
        print(f"[!] НЕТ: {tag}"); ok = False; return s
    if s.count(old) != 1:
        print(f"[!] НЕОДНОЗНАЧНО ({s.count(old)}): {tag}"); ok = False; return s
    print(f"[+] OK: {tag}")
    return s.replace(old, new, 1)

# 1) toplevel: если не int — парсить как stmt (main-body)
old = '''    def toplevel(self):
        # int name(...)
        # int name;
        # int name = expr;
        # int name[N];
        self.eat('int')
        name = self.eat()'''
new = '''    def toplevel(self):
        # Если не 'int' — это statement верхнего уровня (main-body)
        if self.peek() != 'int':
            return self.stmt()
        self.eat('int')
        name = self.eat()'''
src = sub(src, old, new, "toplevel: main-body statements")

# 2) index в gen — доделать
old = '''        if k == 'index':
            # R6 + (base + idx*4)
            name, idx = n[1], n[2]
            base = self.offset_of(name)
            self.gen(idx)                    # R0 = idx
            self.e.shli(0, 2)                # R0 = idx*4
            self.e.addi(0, base)             # R0 = base + idx*4
            self.e.ldl(0, 6, 0)  # ← ошибка
            # Надо: загрузить [R6 + R0] — нет такой адресации.
            # Упростим: R1 = R6 + R0, потом LDL R0, [R1+0]
            # Перезапишем:
            # правильно:
            return'''
new = '''        if k == 'index':
            # R0 = [R6 + base + idx*4]
            name, idx = n[1], n[2]
            base = self.offset_of(name)
            self.gen(idx)                    # R0 = idx
            self.e.shli(0, 2)                # R0 = idx*4
            self.e.addi(0, base)             # R0 = base + idx*4
            self.e.add(1, 6, 0)              # R1 = R6 + R0
            self.e.ldl(0, 1, 0)              # R0 = [R1+0]
            return'''
src = sub(src, old, new, "index load")

# 3) gen_program: обход всех не-func items как statements
old = '''    def gen_program(self, ast):
        # 1) собрать функции
        for item in ast[1]:
            if item[0] == 'func':
                self.collect(item)
        # 2) пролог
        self.e.movi(6, GLOBAL_BASE)
        self.e.movi(7, STACK_TOP)
        # 3) все декларации верхнего уровня в main-поток
        for item in ast[1]:
            if item[0] == 'decl':
                self.stmt(item)
            elif item[0] == 'decl_array':
                self.stmt(item)
        # 4) тела функций'''
new = '''    def gen_program(self, ast):
        # 1) собрать функции
        for item in ast[1]:
            if item[0] == 'func':
                self.collect(item)
        # 2) пролог
        self.e.movi(6, GLOBAL_BASE)
        self.e.movi(7, STACK_TOP)
        # 3) все не-func items → main-body
        for item in ast[1]:
            if item[0] != 'func':
                self.stmt(item)
        # 4) тела функций'''
src = sub(src, old, new, "gen_program: все top-level stmt")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО")
