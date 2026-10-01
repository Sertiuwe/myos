import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()
ok = True

def sub_once(s, old, new, tag):
    if old not in s:
        print(f"[!] нет: {tag}"); return s, False
    if s.count(old) != 1:
        print(f"[!] неоднозначно ({s.count(old)}): {tag}"); return s, False
    print(f"[+] {tag}")
    return s.replace(old, new, 1), True

# 1) include
if '#include "cache.h"' not in src:
    src, r = sub_once(src, '#include "virtnet.h"\n',
                      '#include "virtnet.h"\n#include "cache.h"\n',
                      'include cache.h')
    ok &= r
else:
    print("[=] include уже есть")

# 2) поля в Memory
if 'Cache icache{"I"};' not in src:
    old = '''class Memory {
public:
    std::vector<uint8_t> data;
'''
    new = '''class Memory {
public:
    std::vector<uint8_t> data;
    Cache icache{"I"};
    Cache dcache{"D"};
'''
    src, r = sub_once(src, old, new, 'поля кешей'); ok &= r
else:
    print("[=] поля уже есть")

# 3) attach в конструкторе
if 'icache.attach(' not in src:
    old = '    Memory() : data(MEM_SIZE, 0) {}\n'
    new = ('    Memory() : data(MEM_SIZE, 0) {\n'
           '        icache.attach(data.data(), MEM_SIZE);\n'
           '        dcache.attach(data.data(), MEM_SIZE);\n'
           '    }\n')
    src, r = sub_once(src, old, new, 'attach в конструкторе'); ok &= r
else:
    print("[=] attach уже есть")

# 4) объявления read32_ic / read16_ic
if 'uint32_t read32_ic(uint32_t a);' not in src:
    old = '''    uint32_t read32(uint32_t a);
    uint16_t read16(uint32_t a);
    uint8_t  read8 (uint32_t a);
'''
    new = '''    uint32_t read32(uint32_t a);
    uint16_t read16(uint32_t a);
    uint8_t  read8 (uint32_t a);
    uint32_t read32_ic(uint32_t a);
    uint16_t read16_ic(uint32_t a);
'''
    src, r = sub_once(src, old, new, 'объявления read32_ic'); ok &= r
else:
    print("[=] объявления уже есть")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
