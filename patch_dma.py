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

# ---- 1. Emulator: rebind() с attach_shared/attach_arm4 ----
old = '''    Emulator() {
        mem.ports = &ports;
        mem.emu = this;
        g_cpu_ptr = &cpu;
        g_cp15_ptr = &cp15;
        g_mmu_ptr  = &mmu;
        mmu.attach(mem.data.data(), MEM_SIZE);
        cp15.icache_ptr = &mem.icache;
        cp15.dcache_ptr = &mem.dcache;
    }'''
new = '''    void rebind() {
        mem.ports = &ports;
        mem.emu = this;
        g_cpu_ptr = &cpu;
        g_cp15_ptr = &cp15;
        g_mmu_ptr  = &mmu;
        mmu.attach(mem.data.data(), MEM_SIZE);
        cp15.icache_ptr = &mem.icache;
        cp15.dcache_ptr = &mem.dcache;
        // shared окно VMCU: ARM4 0x70000..0x70FFF <-> VMCU 0x8000..0x8FFF
        vmcu.attach_shared(mem.data.data() + 0x70000);
        vmcu.attach_arm4(mem.data.data(), MEM_SIZE);
    }

    Emulator() { rebind(); }'''
src = sub_once(src, old, new, "Emulator: rebind() с shared+DMA")

# ---- 2. reset в shell: добавить rebind ----
old = '''        if (std::strcmp(cmd, "reset") == 0) { emu = Emulator(); std::printf("Сброшено.\\n"); return false; }'''
new = '''        if (std::strcmp(cmd, "reset") == 0) {
            emu = Emulator();
            emu.rebind();
            std::printf("Сброшено.\\n");
            return false;
        }'''
src = sub_once(src, old, new, "reset: rebind после копирования")

# ---- 3. Memory: bypass dcache для shared 0x70000-0x70FFF (6 методов) ----
SHARED_BYPASS = '''    // shared ARM4<->VMCU регион 0x70000-0x70FFF — без кеша
    if (a >= 0x70000 && a < 0x71000) {
        if (!valid32(a)) { flag_fault(a, false); return 0; }
        uint32_t v; std::memcpy(&v, &data[a], 4); return v;
    }
'''

old = '''    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return dcache.read32(a);
}
uint16_t Memory::read16(uint32_t a) {'''
new = '''    if (!valid32(a)) { flag_fault(a, false); return 0; }
''' + SHARED_BYPASS + '''    return dcache.read32(a);
}
uint16_t Memory::read16(uint32_t a) {'''
src = sub_once(src, old, new, "Memory::read32 bypass")

old = '''    if (!valid16(a)) { flag_fault(a, false); return 0; }
    return dcache.read16(a);
}
uint8_t Memory::read8(uint32_t a) {'''
new = '''    if (!valid16(a)) { flag_fault(a, false); return 0; }
    if (a >= 0x70000 && a < 0x71000) {
        uint16_t v; std::memcpy(&v, &data[a], 2); return v;
    }
    return dcache.read16(a);
}
uint8_t Memory::read8(uint32_t a) {'''
src = sub_once(src, old, new, "Memory::read16 bypass")

old = '''    if (!valid8(a)) { flag_fault(a, false); return 0; }
    return dcache.read8(a);
}'''
new = '''    if (!valid8(a)) { flag_fault(a, false); return 0; }
    if (a >= 0x70000 && a < 0x71000) return data[a];
    return dcache.read8(a);
}'''
src = sub_once(src, old, new, "Memory::read8 bypass")

old = '''    if (!valid32(a)) { flag_fault(a, true); return; }
    dcache.write32(a, v);
}
void Memory::write16'''
new = '''    if (!valid32(a)) { flag_fault(a, true); return; }
    if (a >= 0x70000 && a < 0x71000) {
        std::memcpy(&data[a], &v, 4); return;
    }
    dcache.write32(a, v);
}
void Memory::write16'''
src = sub_once(src, old, new, "Memory::write32 bypass")

old = '''    if (!valid16(a)) { flag_fault(a, true); return; }
    dcache.write16(a, v);
}
void Memory::write8'''
new = '''    if (!valid16(a)) { flag_fault(a, true); return; }
    if (a >= 0x70000 && a < 0x71000) {
        std::memcpy(&data[a], &v, 2); return;
    }
    dcache.write16(a, v);
}
void Memory::write8'''
src = sub_once(src, old, new, "Memory::write16 bypass")

old = '''    if (!valid8(a)) { flag_fault(a, true); return; }
    dcache.write8(a, v);
}'''
new = '''    if (!valid8(a)) { flag_fault(a, true); return; }
    if (a >= 0x70000 && a < 0x71000) { data[a] = v; return; }
    dcache.write8(a, v);
}'''
src = sub_once(src, old, new, "Memory::write8 bypass")

# ---- 4. Shell: новые подкоманды vmcu ----
old = '''            std::printf("vmcu dump|run|halt|reset|step [n]|call <addr>|mem <a> <v>|load <a> <hex>|regs|setreg <n> <v>|setpc <a>|fdump|fset <n> <bits>|fget <n>|fsetf <n> <val>\\n");
            return false;
        }
'''
new = '''            if (std::strcmp(arg1, "rd") == 0) {
                if (!arg2[0]) { std::printf("vmcu rd <addr>\\n"); return false; }
                uint32_t a = parse_u32(arg2) & 0xFFFF;
                uint32_t w = v.mem_read32(a);
                std::printf("[0x%04X] = 0x%08X\\n", a, w);
                return false;
            }
            if (std::strcmp(arg1, "rdb") == 0) {
                if (!arg2[0]) { std::printf("vmcu rdb <addr>\\n"); return false; }
                uint32_t a = parse_u32(arg2) & 0xFFFF;
                uint8_t b = v.mem_read8(a);
                std::printf("[0x%04X] = 0x%02X (%u)\\n", a, b, (unsigned)b);
                return false;
            }
            if (std::strcmp(arg1, "rdh") == 0) {
                if (!arg2[0]) { std::printf("vmcu rdh <addr>\\n"); return false; }
                uint32_t a = parse_u32(arg2) & 0xFFFF;
                uint16_t h = v.mem_read16(a);
                std::printf("[0x%04X] = 0x%04X (%u)\\n", a, h, (unsigned)h);
                return false;
            }
            if (std::strcmp(arg1, "wr") == 0) {
                char ma[32]={0}, mv[32]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %31s %31s", ma, mv);
                if (!ma[0] || !mv[0]) { std::printf("vmcu wr <addr> <val>\\n"); return false; }
                uint32_t a = parse_u32(ma) & 0xFFFF;
                uint32_t val = parse_u32(mv);
                v.mem_write32(a, val);
                std::printf("VMCU MEM[0x%04X] = 0x%08X\\n", a, val);
                return false;
            }
            if (std::strcmp(arg1, "dumpmem") == 0) {
                char ma[32]={0}, mc[16]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %31s %15s", ma, mc);
                uint32_t a = ma[0] ? parse_u32(ma) : 0;
                int n = mc[0] ? std::atoi(mc) : 8;
                if (n > 256) n = 256;
                std::printf("VMCU MEM 0x%04X..0x%04X (\\u0441\\u043b\\u043e\\u0432: %d)\\n",
                            a, a + n*4 - 4, n);
                for (int i = 0; i < n; i += 4) {
                    std::printf("  [0x%04X]:", a + i*4);
                    for (int k = 0; k < 4 && i + k < n; k++) {
                        uint32_t w = v.mem_read32((a + (i+k)*4) & 0xFFFF);
                        std::printf(" %08X", w);
                    }
                    std::printf("\\n");
                }
                return false;
            }
            if (std::strcmp(arg1, "dma") == 0) {
                char a1[32]={0}, a2[32]={0}, a3[32]={0}, a4[16]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %31s %31s %31s %15s",
                            a1, a2, a3, a4);
                if (!a1[0] || !a2[0] || !a3[0]) {
                    std::printf("vmcu dma <src> <dst> <len> <dir>\\n");
                    std::printf("  dir=0: ARM4[src] -> VMCU[dst]\\n");
                    std::printf("  dir=1: VMCU[src] -> ARM4[dst]\\n");
                    return false;
                }
                uint32_t s = parse_u32(a1);
                uint32_t d = parse_u32(a2);
                uint32_t l = parse_u32(a3);
                uint32_t dir = a4[0] ? parse_u32(a4) : 0;
                v.dma_src = s; v.dma_dst = d; v.dma_len = l; v.dma_dir = dir & 1;
                v.do_dma();
                std::printf("DMA %s: 0x%08X -> 0x%08X, %u \\u0431\\u0430\\u0439\\u0442, stat=%u\\n",
                            (dir & 1) ? "VMCU->ARM4" : "ARM4->VMCU",
                            s, d, l, v.dma_stat);
                return false;
            }
            if (std::strcmp(arg1, "shared") == 0) {
                std::printf("--- Shared memory (VMCU 0x8000..0x8FFF <-> ARM4 0x70000) ---\\n");
                for (int i = 0; i < 32; i += 4) {
                    std::printf("  [0x%04X]:", 0x8000 + i*4);
                    for (int k = 0; k < 4; k++) {
                        uint32_t w = v.mem_read32(0x8000 + (i+k)*4);
                        std::printf(" %08X", w);
                    }
                    std::printf("\\n");
                }
                return false;
            }
            std::printf("vmcu dump|run|halt|reset|step [n]|call <addr>|mem <a> <v>|load <a> <hex>|regs|setreg <n> <v>|setpc <a>|fdump|fset <n> <bits>|fget <n>|fsetf <n> <val>|rd <a>|rdb <a>|rdh <a>|wr <a> <v>|dumpmem <a> <n>|dma <src> <dst> <len> <dir>|shared\\n");
            return false;
        }
'''
src = sub_once(src, old, new, "shell: vmcu rd/rdb/rdh/wr/dumpmem/dma/shared")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО")
