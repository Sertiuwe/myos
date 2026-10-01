import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''    if (!valid32(a)) { flag_fault(a, false); return 0; }
    uint32_t v; std::memcpy(&v, &data[a], 4); return v;
}
uint16_t Memory::read16(uint32_t a) {'''

new = '''    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return dcache.read32(a);
}
uint16_t Memory::read16(uint32_t a) {'''

if old in src:
    src = src.replace(old, new, 1)
    print("[+] OK: read32 -> dcache.read32")
elif 'return dcache.read32(a);' in src:
    print("[=] read32 уже через dcache — ничего не надо")
else:
    print("[!] Шаблон не найден, нужна ручная проверка")
    raise SystemExit(1)

# Проверим остальные места на всякий случай
import re
bad = []
if re.search(r'uint32_t v; std::memcpy\(&v, &data\[a\], 4\)', src): bad.append("read32 (memcpy)")
if re.search(r'uint16_t v; std::memcpy\(&v, &data\[a\], 2\)', src): bad.append("read16 (memcpy)")
if re.search(r'return data\[a\];', src): bad.append("read8 (direct)")
if re.search(r'std::memcpy\(&data\[a\], &v, 4\)', src): bad.append("write32 (memcpy)")
if re.search(r'std::memcpy\(&data\[a\], &v, 2\)', src): bad.append("write16 (memcpy)")
if re.search(r'data\[a\] = v;', src): bad.append("write8 (direct)")

if bad:
    print("[!] Остались прямые обращения:")
    for b in bad: print(f"    - {b}")
else:
    print("[+] Все 6 функций идут через dcache")

p.write_text(src)
