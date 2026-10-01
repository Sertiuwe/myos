import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()
ok = True

def sub(s, old, new, tag):
    global ok
    if old not in s:
        print(f"[!] нет: {tag}"); ok = False; return s
    if s.count(old) != 1:
        print(f"[!] неоднозначно ({s.count(old)}): {tag}"); ok = False; return s
    print(f"[+] {tag}")
    return s.replace(old, new, 1)

# mmu_xlate: добавить аргумент fetch
old = '''static inline uint32_t mmu_xlate(Memory* /*m*/, uint32_t a, bool write, bool& fault) {
    fault = false;
    if (!g_cp15_ptr || !g_cp15_ptr->mmu_on()) return a;
    // MMIO и порты транслируем напрямую (Device memory)
    if (a >= 0x10000000u && a < 0x10200000u) return a;
    if (a >= 0xF0000000u && a < 0xF0001000u) return a;
    return g_mmu_ptr->translate(a, write, fault, *g_cp15_ptr);
}'''

new = '''static inline uint32_t mmu_xlate(Memory* /*m*/, uint32_t a, bool write, bool& fault,
                                   bool fetch = false) {
    fault = false;
    if (!g_cp15_ptr || !g_cp15_ptr->mmu_on()) return a;
    if (a >= 0x10000000u && a < 0x10200000u) return a;   // MMIO
    if (a >= 0xF0000000u && a < 0xF0001000u) return a;   // ports
    int code = 0;
    return g_mmu_ptr->translate(a, write, false, fetch, fault, code, *g_cp15_ptr);
}'''
src = sub(src, old, new, 'mmu_xlate + fetch')

# Заменяем вызовы в read32/16/8 и write32/16/8 — добавляем fetch
src = src.replace(
    'uint32_t pa = mmu_xlate(this, a, false, f);\n        if (f) { flag_fault(a, false); return 0; }\n        a = pa;\n    }\n    if (emu) {\n        if (emu->vic.contains(a))    return emu->vic.read(a, 4);',
    'uint32_t pa = mmu_xlate(this, a, false, f, false);\n        if (f) { flag_fault(a, false); return 0; }\n        a = pa;\n    }\n    if (emu) {\n        if (emu->vic.contains(a))    return emu->vic.read(a, 4);', 1)

src = src.replace(
    'uint32_t pa = mmu_xlate(this, a, false, f);\n        if (f) { flag_fault(a, false); return 0; }\n        a = pa;\n    }\n    if (emu) {\n        if (emu->vic.contains(a))    return (uint16_t)emu->vic.read(a, 2);',
    'uint32_t pa = mmu_xlate(this, a, false, f, false);\n        if (f) { flag_fault(a, false); return 0; }\n        a = pa;\n    }\n    if (emu) {\n        if (emu->vic.contains(a))    return (uint16_t)emu->vic.read(a, 2);', 1)

src = src.replace(
    'uint32_t pa = mmu_xlate(this, a, false, f);\n        if (f) { flag_fault(a, false); return 0; }\n        a = pa;\n    }\n    if (emu) {\n        if (emu->vic.contains(a))    return (uint8_t)emu->vic.read(a, 1);',
    'uint32_t pa = mmu_xlate(this, a, false, f, false);\n        if (f) { flag_fault(a, false); return 0; }\n        a = pa;\n    }\n    if (emu) {\n        if (emu->vic.contains(a))    return (uint8_t)emu->vic.read(a, 1);', 1)

src = src.replace('uint32_t pa = mmu_xlate(this, a, true, f);',
                  'uint32_t pa = mmu_xlate(this, a, true, f, false);')

# read32_ic / read16_ic — fetch с MMU
old = '''uint32_t Memory::read32_ic(uint32_t a) {
    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return icache.read32(a);
}'''
new = '''uint32_t Memory::read32_ic(uint32_t a) {
    if (g_cp15_ptr && g_cp15_ptr->mmu_on()) {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, false, f, true);
        if (f) { flag_fault(a, false); return 0; }
        a = pa;
    }
    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return icache.read32(a);
}'''
src = sub(src, old, new, 'read32_ic MMU')

old = '''uint16_t Memory::read16_ic(uint32_t a) {
    if (!valid16(a)) { flag_fault(a, false); return 0; }
    return icache.read16(a);
}'''
new = '''uint16_t Memory::read16_ic(uint32_t a) {
    if (g_cp15_ptr && g_cp15_ptr->mmu_on()) {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, false, f, true);
        if (f) { flag_fault(a, false); return 0; }
        a = pa;
    }
    if (!valid16(a)) { flag_fault(a, false); return 0; }
    return icache.read16(a);
}'''
src = sub(src, old, new, 'read16_ic MMU')

# Новые команды mmu map / protect / test
old = '''        if (std::strcmp(cmd, "tlb") == 0) {'''
new = '''        if (std::strcmp(cmd, "mmu") == 0 && arg1[0] == 'm' && arg1[1] == 'a' && arg1[2] == 'p') {
            // mmu map <virt> <phys> [size_in_kb]
            char a1[32]={0}, a2[32]={0}, a3[16]={0};
            std::sscanf(line_str.c_str(), "%*s %*s %31s %31s %15s", a1, a2, a3);
            uint32_t va = parse_u32(a1);
            uint32_t pa = parse_u32(a2);
            uint32_t sz = a3[0] ? parse_u32(a3) : 4;
            if (va & 0xFFF || pa & 0xFFF) { std::printf("выравнивание по 4K\\n"); return false; }
            uint32_t ttbr = emu.cp15.ttbr0;
            // Маппим через coarse L2
            uint32_t l1i = va >> 20;
            uint32_t l1 = emu.mem.read32(ttbr + l1i*4);
            uint32_t l2base;
            if ((l1 & 3) == 1) {
                l2base = l1 & 0xFFFFFC00u;
            } else {
                // Выделяем L2-таблицу после TTBR + 16КБ
                l2base = ttbr + 0x4000 + (l1i << 10);
                emu.mem.write32(ttbr + l1i*4, l2base | 0x00000011u);  // coarse, domain 0
                for (int i = 0; i < 256; i++) emu.mem.write32(l2base + i*4, 0);
            }
            for (uint32_t off = 0; off < sz*1024; off += 0x1000) {
                uint32_t l2i = ((va+off) >> 12) & 0xFF;
                uint32_t desc = ((pa+off) & 0xFFFFF000u) | 0x00000002u | (3u << 4) | 0x10;  // rw, user
                emu.mem.write32(l2base + l2i*4, desc);
            }
            g_mmu_ptr->flush();
            std::printf("mapped 0x%08X -> 0x%08X, %u KB (L2=0x%08X)\\n", va, pa, sz, l2base);
            return false;
        }
        if (std::strcmp(cmd, "tlb") == 0) {'''
src = sub(src, old, new, 'mmu map')

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
