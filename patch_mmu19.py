import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()
ok = True

def sub_once(s, old, new, tag):
    global ok
    if old not in s:
        print(f"[!] НЕТ: {tag}"); ok = False; return s
    n = s.count(old)
    if n != 1:
        print(f"[!] НЕОДНОЗНАЧНО ({n}x): {tag}"); ok = False; return s
    print(f"[+] OK: {tag}")
    return s.replace(old, new, 1)

# ---------------------------------------------------------------
# 1. Связка Cache* с CP15 в конструкторе Emulator
# ---------------------------------------------------------------
old = '''    Emulator() {
        mem.ports = &ports;
        mem.emu = this;
        g_cpu_ptr = &cpu;
        g_cp15_ptr = &cp15;
        g_mmu_ptr  = &mmu;
        mmu.attach(mem.data.data(), MEM_SIZE);
    }'''
new = '''    Emulator() {
        mem.ports = &ports;
        mem.emu = this;
        g_cpu_ptr = &cpu;
        g_cp15_ptr = &cp15;
        g_mmu_ptr  = &mmu;
        mmu.attach(mem.data.data(), MEM_SIZE);
        // Связка CP15 <-> Cache для реальных c7-инвалидаций
        cp15.icache_ptr = &mem.icache;
        cp15.dcache_ptr = &mem.dcache;
    }'''
src = sub_once(src, old, new, "Emulator: cp15<->cache")

# ---------------------------------------------------------------
# 2. mmu_xlate: передавать user-флаг из текущего режима CPU
# ---------------------------------------------------------------
old = '''static inline uint32_t mmu_xlate(Memory* /*m*/, uint32_t a, bool write, bool& fault,
                                   bool fetch = false) {
    fault = false;
    if (!g_cp15_ptr || !g_cp15_ptr->mmu_on()) return a;
    if (a >= 0x10000000u && a < 0x10200000u) return a;   // MMIO
    if (a >= 0xF0000000u && a < 0xF0001000u) return a;   // ports
    int code = 0;
    return g_mmu_ptr->translate(a, write, false, fetch, fault, code, *g_cp15_ptr);
}'''
new = '''static inline uint32_t mmu_xlate(Memory* /*m*/, uint32_t a, bool write, bool& fault,
                                   bool fetch = false) {
    fault = false;
    if (!g_cp15_ptr || !g_cp15_ptr->mmu_on()) return a;
    if (a >= 0x10000000u && a < 0x10200000u) return a;   // MMIO
    if (a >= 0xF0000000u && a < 0xF0001000u) return a;   // ports
    // User-доступ если CPU в User mode
    bool user = false;
    if (g_cpu_ptr) {
        Mode m = g_cpu_ptr->cpsr.mode();
        user = (m == Mode::User);
    }
    int code = 0;
    return g_mmu_ptr->translate(a, write, user, fetch, fault, code, *g_cp15_ptr);
}'''
src = sub_once(src, old, new, "mmu_xlate: user-флаг")

# ---------------------------------------------------------------
# 3. Команда "mmu" — расширенный help + dump
# ---------------------------------------------------------------
old = '''                } else {
                    std::printf("mmu on|off|flush|setup [ttbr]|map <v> <p> [kb]|dump|tlb\\n");
                }'''
new = '''                } else if (std::strcmp(arg1, "reset") == 0) {
                    emu.cp15 = CP15();
                    emu.mmu.flush();
                    emu.cp15.icache_ptr = &emu.mem.icache;
                    emu.cp15.dcache_ptr = &emu.mem.dcache;
                    std::printf("CP15 + MMU reset\\n");
                } else if (std::strcmp(arg1, "stats") == 0) {
                    emu.mmu.dump();
                } else {
                    std::printf("mmu on|off|flush|reset|setup [ttbr]|map <v> <p> [kb]|dump|stats|tlb\\n");
                }'''
src = sub_once(src, old, new, "mmu: reset/stats")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО")
