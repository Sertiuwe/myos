import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''    // VIC базовый
    {
        Emulator emu;
        uint32_t prog[] = {
            0xE59F0018,  // LDR r0, [pc, #24]
            0xE3A01A12,  // MOV r1, #0x1234
            0xE5801100,  // STR r1, [r0, #0x100]
            0xE3A0102C,  // MOV r1, #0x2C
            0xE5801200,  // STR r1, [r0, #0x200]
            0xE3A01A01,  // MOV r1, #0x1000
            0xE5801010,  // STR r1, [r0, #0x10]
            0xEAFFFFFE,  // B .
            0x10140000
        };
        for (size_t i = 0; i < sizeof(prog)/4; i++) emu.mem.write32(i*4, prog[i]);
        emu.run(100, false);
        std::printf("VIC test: VectAddr=0x%08X (ожид. 0x1234)\\n", emu.vic.vect_addr);
        std::printf("  IntEnable=0x%08X (ожид. 0x1000)\\n", emu.vic.int_enable);
        std::printf("  VectCntl[0]=0x%02X  IRQStatus=0x%08X\\n\\n",
                    emu.vic.vect_cntl_reg[0], emu.vic.irq_status);
    }'''

new = '''    // VIC базовый — VectAddrReg + VectCntl + IntEnable + SoftInt
    {
        Emulator emu;
        uint32_t prog[] = {
            0xE59F001C,  // 0x00 LDR r0, [pc, #28]  ; r0 = 0x10140000
            0xE59F101C,  // 0x04 LDR r1, [pc, #28]  ; r1 = 0x00008000
            0xE5801100,  // 0x08 STR r1, [r0, #0x100]  ; VectAddrReg[0] = 0x8000
            0xE3A0102C,  // 0x0C MOV r1, #0x2C
            0xE5801200,  // 0x10 STR r1, [r0, #0x200]  ; VectCntl[0] = 0x20|12
            0xE3A01A01,  // 0x14 MOV r1, #0x1000
            0xE5801010,  // 0x18 STR r1, [r0, #0x10]   ; IntEnable = 0x1000
            0xE5801018,  // 0x1C STR r1, [r0, #0x18]   ; SoftInt  = 0x1000
            0xEAFFFFFE,  // 0x20 B .
            0x10140000,  // 0x24 data: VIC base
            0x00008000   // 0x28 data: vector
        };
        for (size_t i = 0; i < sizeof(prog)/4; i++) emu.mem.write32(i*4, prog[i]);
        emu.run(100, false);
        std::printf("VIC test: VectAddr=0x%08X (ожид. 0x8000)\\n", emu.vic.vect_addr);
        std::printf("  IntEnable=0x%08X (ожид. 0x1000)\\n", emu.vic.int_enable);
        std::printf("  VectCntl[0]=0x%02X  IRQStatus=0x%08X (ожид. 0x1000)\\n\\n",
                    emu.vic.vect_cntl_reg[0], emu.vic.irq_status);
    }'''

if old not in src:
    print("[!] VIC test block not found")
    raise SystemExit(1)
src = src.replace(old, new, 1)
p.write_text(src)
print("[+] VIC test rewritten")
