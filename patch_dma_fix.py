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

# 1) Emulator: точное совпадение конструктора
old_ctor = '''    Emulator() {
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
new_ctor = '''    void rebind() {
        mem.ports = &ports;
        mem.emu = this;
        g_cpu_ptr = &cpu;
        g_cp15_ptr = &cp15;
        g_mmu_ptr  = &mmu;
        mmu.attach(mem.data.data(), MEM_SIZE);
        // Связка CP15 <-> Cache для реальных c7-инвалидаций
        cp15.icache_ptr = &mem.icache;
        cp15.dcache_ptr = &mem.dcache;
        // Shared окно + DMA
        vmcu.attach_shared(mem.data.data() + 0x70000);
        vmcu.attach_arm4(mem.data.data(), MEM_SIZE);
    }

    Emulator() { rebind(); }'''
src = sub_once(src, old_ctor, new_ctor, "Emulator -> rebind")

# 2) Memory::read16 — точное совпадение конца
old_r16 = '''    if (!valid16(a)) { flag_fault(a, false); return 0; }
    return dcache.read16(a);
}'''
new_r16 = '''    if (!valid16(a)) { flag_fault(a, false); return 0; }
    if (a >= 0x70000 && a < 0x71000) {
        uint16_t v; std::memcpy(&v, &data[a], 2); return v;
    }
    return dcache.read16(a);
}'''
src = sub_once(src, old_r16, new_r16, "Memory::read16 bypass")

# 3) Memory::read16_ic — тоже пропустить shared (иначе fetch из shared не сработает)
old_r16ic = '''uint16_t Memory::read16_ic(uint32_t a) {
    if (g_cp15_ptr && g_cp15_ptr->mmu_on()) {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, false, f, true);
        if (f) { flag_fault(a, false, true); return 0; }
        a = pa;
    }
    if (!valid16(a)) { flag_fault(a, false); return 0; }
    return icache.read16(a);
}'''
new_r16ic = '''uint16_t Memory::read16_ic(uint32_t a) {
    if (g_cp15_ptr && g_cp15_ptr->mmu_on()) {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, false, f, true);
        if (f) { flag_fault(a, false, true); return 0; }
        a = pa;
    }
    if (!valid16(a)) { flag_fault(a, false); return 0; }
    if (a >= 0x70000 && a < 0x71000) {
        uint16_t v; std::memcpy(&v, &data[a], 2); return v;
    }
    return icache.read16(a);
}'''
if 'if (a >= 0x70000 && a < 0x71000)' in src.split('uint16_t Memory::read16_ic')[1][:400] if 'uint16_t Memory::read16_ic' in src else False:
    print("[=] read16_ic уже с bypass")
else:
    src = sub_once(src, old_r16ic, new_r16ic, "Memory::read16_ic bypass")

# 4) Memory::read32_ic
old_r32ic = '''uint32_t Memory::read32_ic(uint32_t a) {
    if (g_cp15_ptr && g_cp15_ptr->mmu_on()) {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, false, f, true);
        if (f) { flag_fault(a, false, true); return 0; }
        a = pa;
    }
    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return icache.read32(a);
}'''
new_r32ic = '''uint32_t Memory::read32_ic(uint32_t a) {
    if (g_cp15_ptr && g_cp15_ptr->mmu_on()) {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, false, f, true);
        if (f) { flag_fault(a, false, true); return 0; }
        a = pa;
    }
    if (!valid32(a)) { flag_fault(a, false); return 0; }
    if (a >= 0x70000 && a < 0x71000) {
        uint32_t v; std::memcpy(&v, &data[a], 4); return v;
    }
    return icache.read32(a);
}'''
if 'uint32_t Memory::read32_ic' in src and 'if (a >= 0x70000 && a < 0x71000)' not in src.split('uint32_t Memory::read32_ic')[1][:400]:
    src = sub_once(src, old_r32ic, new_r32ic, "Memory::read32_ic bypass")
else:
    print("[=] read32_ic уже с bypass")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО")
