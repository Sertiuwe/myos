import pathlib, re

# ---------- mmu.h: extern указатели ----------
p = pathlib.Path("mmu.h")
src = p.read_text()

if 'extern CP15* g_cp15_ptr;' not in src:
    src = src.replace('#endif\n',
                      'extern CP15* g_cp15_ptr;\nextern MMU*  g_mmu_ptr;\n\n#endif\n', 1)
    print("[+] mmu.h: extern указатели")

p.write_text(src)

# ---------- ARM4.cpp ----------
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

# 1) поля cp15+mmu в Emulator (после VirtEth)
if 'CP15       cp15;' not in src:
    m = re.search(r'(VirtEth\s+eth\{[^}]+\};\n)', src)
    if m:
        src = src.replace(m.group(1),
                          m.group(1) + '    CP15       cp15;\n    MMU        mmu;\n', 1)
        print("[+] cp15+mmu в Emulator")
    else:
        print("[!] VirtEth не найден")
else:
    print("[=] cp15 уже есть")

# 2) глобальные указатели (после g_cpu_ptr = nullptr)
if 'CP15* g_cp15_ptr = nullptr;' not in src:
    src = src.replace('CPU* g_cpu_ptr = nullptr;\n',
                      'CPU* g_cpu_ptr = nullptr;\n'
                      'CP15* g_cp15_ptr = nullptr;\n'
                      'MMU*  g_mmu_ptr  = nullptr;\n', 1)
    print("[+] глобальные указатели")

# 3) инициализация в конструкторе Emulator
if 'g_cp15_ptr = &cp15;' not in src:
    old = '        g_cpu_ptr = &cpu;\n        mmu.attach(mem.data.data(), MEM_SIZE);\n'
    new = ('        g_cpu_ptr = &cpu;\n'
           '        g_cp15_ptr = &cp15;\n'
           '        g_mmu_ptr  = &mmu;\n'
           '        mmu.attach(mem.data.data(), MEM_SIZE);\n')
    if old in src:
        src = src.replace(old, new, 1)
        print("[+] инициализация указателей")
    else:
        print("[!] не найден anchor g_cpu_ptr + mmu.attach")

# 4) mmu_xlate: переписать через глобальные указатели
old_xlate = '''static inline uint32_t mmu_xlate(Memory* m, uint32_t a, bool write, bool& fault) {
    fault = false;
    if (!m->emu || !m->emu->cp15.mmu_on()) return a;
    uint32_t pa = m->emu->mmu.translate(a, write, fault, m->emu->cp15);
    return pa;
}'''
new_xlate = '''static inline uint32_t mmu_xlate(Memory* /*m*/, uint32_t a, bool write, bool& fault) {
    fault = false;
    if (!g_cp15_ptr || !g_cp15_ptr->mmu_on()) return a;
    return g_mmu_ptr->translate(a, write, fault, *g_cp15_ptr);
}'''
if old_xlate in src:
    src = src.replace(old_xlate, new_xlate, 1)
    print("[+] mmu_xlate через g_cp15_ptr")

p.write_text(src)
print("готово")
