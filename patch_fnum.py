import pathlib
import re

p = pathlib.Path("epcc.py")
if not p.exists():
    print("[!] Файл epcc.py не найден")
    exit(1)

src = p.read_text()

# Точное регулярное выражение для поиска оригинального блока (с учетом отступов)
pattern = r'(\s+)if k\s*==\s*fnum\s*:\s*\n\s*off,\s*bits\s*=\s*self\.pool_float\(n\[1\]\)\s*\n\s*self\.e\.fldl\(0,\s*6,\s*off\s*\*\s*4\)\s*#\s*FLDL\s*FO,\s*\[R6\+off\]\s*\n\s*return\s*float'

# Новый код с сохранением исходной табуляции/отступов
def replace_func(match):
    indent = match.group(1)
    return (
        f"{indent}if k == fnum:\n"
        f"{indent}    import struct\n"
        f"{indent}    bits = struct.unpack('<I', struct.pack('<f', n[1]))[0]\n"
        f"{indent}    self.emit_const(0, bits)\n"
        f"{indent}    self.e.stl(6, 0, 0x3FF4)  # scratch для float\n"
        f"{indent}    self.e.fldl(0, 6, 0x3FF4)\n"
        f"{indent}    return float"
    )

new_src, n = re.subn(pattern, replace_func, src)

if n == 1:
    p.write_text(new_src)
    print("[+] fnum: inline-эмиссия через scratch успешно применена")
else:
    print(f"[!] Ошибка: найдено совпадений: {n}. Патч не применен.")
