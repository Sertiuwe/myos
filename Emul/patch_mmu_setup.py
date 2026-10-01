import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''        if (std::strcmp(cmd, "mmu") == 0) {
            if (arg1[0]) {
                if (std::strcmp(arg1, "off") == 0) {
                    emu.cp15.sctlr &= ~1u;
                    std::printf("MMU off\\n");
                } else if (std::strcmp(arg1, "on") == 0) {
                    emu.cp15.sctlr |= 1u;
                    std::printf("MMU on\\n");
                } else if (std::strcmp(arg1, "flush") == 0) {
                    emu.mmu.flush();
                    std::printf("TLB flushed\\n");
                } else {
                    std::printf("mmu on|off|flush|dump|tlb\\n");
                }
                return false;
            }
            emu.mmu.dump();
            return false;
        }'''

new = '''        if (std::strcmp(cmd, "mmu") == 0) {
            if (arg1[0]) {
                if (std::strcmp(arg1, "off") == 0) {
                    emu.cp15.sctlr &= ~1u;
                    emu.mmu.flush();
                    std::printf("MMU off\\n");
                } else if (std::strcmp(arg1, "on") == 0) {
                    emu.cp15.sctlr |= 1u;
                    emu.mmu.flush();
                    std::printf("MMU on\\n");
                } else if (std::strcmp(arg1, "flush") == 0) {
                    emu.mmu.flush();
                    std::printf("TLB flushed\\n");
                } else if (std::strcmp(arg1, "setup") == 0) {
                    uint32_t ttbr = arg2[0] ? parse_u32(arg2) : 0x500000u;
                    uint32_t npages = 32;   // 32 МБ identity map
                    // обнулить таблицу
                    for (uint32_t i = 0; i < 4096; i++) {
                        emu.mem.write32(ttbr + i*4, 0);
                    }
                    // заполнить identity-секции
                    for (uint32_t i = 0; i < npages; i++) {
                        uint32_t pa = i * 0x100000u;
                        uint32_t desc = pa | 0x00000C1Eu;  // section, AP=11, CB=11
                        emu.mem.write32(ttbr + i*4, desc);
                    }
                    emu.cp15.ttbr0 = ttbr;
                    std::printf("MMU setup: TTBR0=0x%08X, %u sections (identity)\\n",
                                ttbr, npages);
                } else {
                    std::printf("mmu on|off|flush|setup [ttbr]|dump|tlb\\n");
                }
                return false;
            }
            emu.mmu.dump();
            return false;
        }'''

if old in src:
    src = src.replace(old, new, 1)
    print("[+] mmu setup добавлена")
else:
    print("[!] не найден mmu")
p.write_text(src)
