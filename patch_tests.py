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

old = r'''    std::printf("=== Готово ===\n\n");
    return 0;
}'''

new = r'''    // === v19 regression tests ===

    // MMU: section translation fault (unmapped VA)
    {
        Emulator emu;
        uint32_t ttbr = 0x500000;
        emu.cp15.ttbr0 = ttbr;
        emu.cp15.dacr = 0x00000001;
        emu.cp15.sctlr |= 1;
        emu.mmu.flush();
        bool fault = false; int code = 0;
        uint32_t pa = emu.mmu.translate(0x00200000u, false, false, false,
                                        fault, code, emu.cp15);
        std::printf("MMU trans fault: fault=%d code=0x%X pa=0x%08X (ожид. 1/0x5/VA)\n",
                    (int)fault, code, pa);
    }

    // MMU: permission fault (priv-only section, user access)
    {
        Emulator emu;
        uint32_t ttbr = 0x500000;
        uint32_t desc = 0x00000412u;   // section, AP=01 (priv), dom=0
        std::memcpy(&emu.mem.data[ttbr], &desc, 4);
        emu.cp15.ttbr0 = ttbr;
        emu.cp15.dacr = 0x00000001;
        emu.cp15.sctlr |= 1;
        emu.mmu.flush();
        bool fault = false; int code = 0;
        emu.mmu.translate(0x00000100u, false, true, false,
                          fault, code, emu.cp15);
        std::printf("MMU perm fault: fault=%d code=0x%X DFSR=0x%X (ожид. 1/0xD)\n",
                    (int)fault, code, emu.cp15.dfsr);
    }

    // MMU: full access + TLB hit
    {
        Emulator emu;
        uint32_t ttbr = 0x500000;
        uint32_t desc = 0x00000C12u;   // section, AP=11 (full), dom=0
        std::memcpy(&emu.mem.data[ttbr], &desc, 4);
        emu.cp15.ttbr0 = ttbr;
        emu.cp15.dacr = 0x00000001;
        emu.cp15.sctlr |= 1;
        emu.mmu.flush();
        bool fault = false; int code = 0;
        uint32_t pa1 = emu.mmu.translate(0x00001234u, false, false, false,
                                         fault, code, emu.cp15);
        uint32_t pa2 = emu.mmu.translate(0x00005678u, false, false, false,
                                         fault, code, emu.cp15);
        std::printf("MMU map: pa1=0x%08X pa2=0x%08X hits=%llu misses=%llu (ожид. 1234/5678/1/1)\n",
                    pa1, pa2,
                    (unsigned long long)emu.mmu.tlb_hits,
                    (unsigned long long)emu.mmu.tlb_misses);
    }

    // Cache: miss then hit
    {
        Emulator emu;
        emu.mem.write32(0x1000, 0xDEADBEEFu);
        emu.mem.dcache.invalidate_all();
        emu.mem.dcache.reset_stats();
        uint32_t v1 = emu.mem.read32(0x1000);
        uint32_t v2 = emu.mem.read32(0x1000);
        std::printf("Cache: v1=0x%08X v2=0x%08X hits=%llu misses=%llu (ожид. DEADBEEF/DEADBEEF/1/1)\n",
                    v1, v2,
                    (unsigned long long)emu.mem.dcache.hits,
                    (unsigned long long)emu.mem.dcache.misses);
    }

    // Thumb: MOV r0,#10 ; ADD r0,#5 ; SUB r0,#3
    {
        Emulator emu;
        // прямая запись в backing buffer, минуя MMU и кеш
        emu.mem.data[0] = 0x0A; emu.mem.data[1] = 0x20;   // 0x200A
        emu.mem.data[2] = 0x05; emu.mem.data[3] = 0x30;   // 0x3005
        emu.mem.data[4] = 0x03; emu.mem.data[5] = 0x38;   // 0x3803
        emu.mem.icache.invalidate_all();
        emu.mem.dcache.invalidate_all();
        emu.cpu.cpsr.set_t(true);
        emu.cpu.r[15] = 0;
        emu.step(); emu.step(); emu.step();
        std::printf("Thumb: r0=0x%08X (ожид. 0xC)\n", emu.cpu.r[0]);
    }

    // FIQ: R8-R12 banking
    {
        Emulator emu;
        emu.cpu.cpsr.set_mode(Mode::User);
        emu.cpu.r[8] = 0xAAAAAAAAu;
        uint32_t before = emu.cpu.get_reg(8);
        emu.cpu.take_exception(0x1C, Mode::FIQ, false);
        emu.cpu.set_reg(8, 0xBBBBBBBBu);
        uint32_t in_fiq = emu.cpu.get_reg(8);
        emu.cpu.cpsr.set_mode(Mode::User);
        uint32_t after = emu.cpu.get_reg(8);
        std::printf("FIQ bank: before=0x%08X in_fiq=0x%08X after=0x%08X banked=0x%08X\n",
                    before, in_fiq, after, emu.cpu.banks[1].r8_12[0]);
        std::printf("  ожид.: AAAAAAAA BBBBBBBB AAAAAAAA BBBBBBBB\n");
    }

    std::printf("=== Готово ===\n\n");
    return 0;
}'''

src = sub_once(src, old, new, "run_tests: v19 regression suite")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО")
