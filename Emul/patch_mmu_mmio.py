import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''static inline uint32_t mmu_xlate(Memory* /*m*/, uint32_t a, bool write, bool& fault) {
    fault = false;
    if (!g_cp15_ptr || !g_cp15_ptr->mmu_on()) return a;
    return g_mmu_ptr->translate(a, write, fault, *g_cp15_ptr);
}'''

new = '''static inline uint32_t mmu_xlate(Memory* /*m*/, uint32_t a, bool write, bool& fault) {
    fault = false;
    if (!g_cp15_ptr || !g_cp15_ptr->mmu_on()) return a;
    // MMIO и порты транслируем напрямую (Device memory)
    if (a >= 0x10000000u && a < 0x10200000u) return a;
    if (a >= 0xF0000000u && a < 0xF0001000u) return a;
    return g_mmu_ptr->translate(a, write, fault, *g_cp15_ptr);
}'''

if old in src:
    src = src.replace(old, new, 1)
    print("[+] MMIO bypass")
else:
    print("[!] mmu_xlate не найден")
p.write_text(src)
