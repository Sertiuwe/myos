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

def ensure(s, marker, old, new, tag):
    if marker in s:
        print(f"[=] уже есть: {tag}")
        return s
    return sub_once(s, old, new, tag)

# 1) include
src = ensure(src, '#include "vmcu.h"',
    '#include "cache.h"\n',
    '#include "cache.h"\n#include "vmcu.h"\n',
    "include vmcu.h")

# 2) member
src = ensure(src, 'VMCU       vmcu{',
    '    VirtEth    eth{0x10016000, "ETH0"};\n',
    '    VirtEth    eth{0x10016000, "ETH0"};\n'
    '    VMCU       vmcu{0x10110000, "VMCU"};\n',
    "member vmcu")

# 3) MMIO routing
pairs = [
 ("if (emu->eth.contains(a))    return emu->eth.read(a, 4);",
  "if (emu->vmcu.contains(a))   return emu->vmcu.read(a, 4);", "MMIO read32"),
 ("if (emu->eth.contains(a))    return (uint16_t)emu->eth.read(a, 2);",
  "if (emu->vmcu.contains(a))   return (uint16_t)emu->vmcu.read(a, 2);", "MMIO read16"),
 ("if (emu->eth.contains(a))    return (uint8_t)emu->eth.read(a, 1);",
  "if (emu->vmcu.contains(a))   return (uint8_t)emu->vmcu.read(a, 1);", "MMIO read8"),
 ("if (emu->eth.contains(a))    { emu->eth.write(a, v, 4); return; }",
  "if (emu->vmcu.contains(a))   { emu->vmcu.write(a, v, 4); return; }", "MMIO write32"),
 ("if (emu->eth.contains(a))    { emu->eth.write(a, v, 2); return; }",
  "if (emu->vmcu.contains(a))   { emu->vmcu.write(a, v, 2); return; }", "MMIO write16"),
 ("if (emu->eth.contains(a))    { emu->eth.write(a, v, 1); return; }",
  "if (emu->vmcu.contains(a))   { emu->vmcu.write(a, v, 1); return; }", "MMIO write8"),
]
for old, tail, tag in pairs:
    if tail in src:
        print(f"[=] уже есть: {tag}"); continue
    if old not in src:
        print(f"[!] НЕТ: {tag}"); ok = False; continue
    src = src.replace(old, old + "\n        " + tail, 1)
    print(f"[+] OK: {tag}")

# 4) tick + VIC source 16
src = ensure(src, 'vmcu.tick();',
    '''        net.tick();
        eth.tick();

        check_interrupts();''',
    '''        net.tick();
        eth.tick();
        vmcu.tick();
        vic.set_source(16, vmcu.irq_pending());

        check_interrupts();''',
    "tick vmcu + VIC source 16")

# 5) shell commands
if 'vmcu dump|run' not in src:
    cmds = r'''        if (std::strcmp(cmd, "vmcu") == 0) {
            VMCU& v = emu.vmcu;
            if (!arg1[0] || std::strcmp(arg1, "dump") == 0) { v.dump(); return false; }
            if (std::strcmp(arg1, "run") == 0)   { v.running = true; v.halted = false;
                                                    std::printf("VMCU running\n"); return false; }
            if (std::strcmp(arg1, "halt") == 0)  { v.running = false;
                                                    std::printf("VMCU halted\n"); return false; }
            if (std::strcmp(arg1, "reset") == 0) { v.do_reset();
                                                    std::printf("VMCU reset\n"); return false; }
            if (std::strcmp(arg1, "step") == 0) {
                int n = arg2[0] ? std::atoi(arg2) : 1;
                for (int i = 0; i < n && v.running && !v.halted; i++) v.step();
                v.dump();
                return false;
            }
            if (std::strcmp(arg1, "call") == 0) {
                if (!arg2[0]) { std::printf("vmcu call <addr>\n"); return false; }
                v.pc = parse_u32(arg2) & 0xFFFF;
                v.running = true; v.halted = false;
                std::printf("VMCU CALL 0x%04X\n", v.pc);
                return false;
            }
            if (std::strcmp(arg1, "mem") == 0) {
                char ma[32]={0}, mv[32]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %31s %31s", ma, mv);
                if (!ma[0] || !mv[0]) { std::printf("vmcu mem <addr> <val>\n"); return false; }
                uint32_t a = parse_u32(ma) & 0xFFFF;
                uint32_t val = parse_u32(mv) & 0xFF;
                v.mem[a] = (uint8_t)val;
                std::printf("VMCU MEM[0x%04X] = 0x%02X\n", a, val);
                return false;
            }
            if (std::strcmp(arg1, "load") == 0) {
                const char* pp = line_str.c_str();
                while (*pp && *pp != ' ') pp++; while (*pp == ' ') pp++;
                while (*pp && *pp != ' ') pp++; while (*pp == ' ') pp++;
                char addr_s[32]={0}; int ai=0;
                while (*pp && *pp != ' ' && ai < 31) addr_s[ai++] = *pp++;
                while (*pp == ' ') pp++;
                uint32_t a = parse_u32(addr_s) & 0xFFFF;
                size_t len = std::strlen(pp);
                if (len == 0 || (len & 1)) { std::printf("vmcu load <addr> <hex>\n"); return false; }
                auto hv = [](char c)->int {
                    if (c>='0'&&c<='9') return c-'0';
                    if (c>='a'&&c<='f') return c-'a'+10;
                    if (c>='A'&&c<='F') return c-'A'+10;
                    return -1;
                };
                int written = 0;
                for (size_t i = 0; i < len; i += 2) {
                    int h = hv(pp[i]), l = hv(pp[i+1]);
                    if (h < 0 || l < 0) { std::printf("не hex\n"); return false; }
                    if (a + written >= VMCU::MEM_SIZE) break;
                    v.mem[(a + written) & 0xFFFF] = (uint8_t)((h << 4) | l);
                    written++;
                }
                std::printf("VMCU: загружено %d байт @ 0x%04X\n", written, a);
                return false;
            }
            if (std::strcmp(arg1, "regs") == 0) {
                std::printf("VMCU R: ");
                for (int i = 0; i < 8; i++) std::printf("R%d=0x%08X ", i, v.r[i]);
                std::printf("\nPC=0x%04X SP=0x%04X FLAGS=0x%X\n", v.pc, v.sp, v.flags);
                return false;
            }
            if (std::strcmp(arg1, "setreg") == 0) {
                char ri[16]={0}, rv[32]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %15s %31s", ri, rv);
                if (!ri[0] || !rv[0]) { std::printf("vmcu setreg <n> <val>\n"); return false; }
                int n = std::atoi(ri);
                if (n < 0 || n > 7) { std::printf("R0..R7\n"); return false; }
                v.r[n] = parse_u32(rv);
                std::printf("VMCU R%d = 0x%08X\n", n, v.r[n]);
                return false;
            }
            if (std::strcmp(arg1, "setpc") == 0) {
                if (!arg2[0]) { std::printf("vmcu setpc <addr>\n"); return false; }
                v.pc = parse_u32(arg2) & 0xFFFF;
                std::printf("VMCU PC = 0x%04X\n", v.pc);
                return false;
            }
            if (std::strcmp(arg1, "fdump") == 0) {
                std::printf("VMCU F: ");
                for (int i = 0; i < 8; i++)
                    std::printf("F%d=%.6g(0x%08X) ", i, (double)v.fp[i],
                                VMCU::float_to_bits(v.fp[i]));
                std::printf("\n");
                return false;
            }
            if (std::strcmp(arg1, "fset") == 0) {
                char fi[16]={0}, fv[32]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %15s %31s", fi, fv);
                if (!fi[0] || !fv[0]) { std::printf("vmcu fset <n> <raw_bits>\n"); return false; }
                int n = std::atoi(fi);
                if (n < 0 || n > 7) { std::printf("F0..F7\n"); return false; }
                v.fp[n] = VMCU::bits_to_float(parse_u32(fv));
                std::printf("VMCU F%d = %.6g\n", n, (double)v.fp[n]);
                return false;
            }
            if (std::strcmp(arg1, "fget") == 0) {
                if (!arg2[0]) { std::printf("vmcu fget <n>\n"); return false; }
                int n = std::atoi(arg2);
                if (n < 0 || n > 7) { std::printf("F0..F7\n"); return false; }
                std::printf("F%d = %.6g  bits=0x%08X\n", n, (double)v.fp[n],
                            VMCU::float_to_bits(v.fp[n]));
                return false;
            }
            if (std::strcmp(arg1, "fsetf") == 0) {
                char fi[16]={0}, fv[64]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %15s %63s", fi, fv);
                if (!fi[0] || !fv[0]) { std::printf("vmcu fsetf <n> <decimal>\n"); return false; }
                int n = std::atoi(fi);
                if (n < 0 || n > 7) { std::printf("F0..F7\n"); return false; }
                v.fp[n] = std::strtof(fv, nullptr);
                std::printf("VMCU F%d = %.6g\n", n, (double)v.fp[n]);
                return false;
            }
            std::printf("vmcu dump|run|halt|reset|step [n]|call <addr>|mem <a> <v>|load <a> <hex>|regs|setreg <n> <v>|setpc <a>|fdump|fset <n> <bits>|fget <n>|fsetf <n> <val>\n");
            return false;
        }
'''
    anchor = '        if (std::strcmp(cmd, "uart0") == 0) {\n'
    if anchor in src:
        src = src.replace(anchor, cmds + anchor, 1)
        print("[+] OK: vmcu shell")
    else:
        print("[!] не найден anchor uart0"); ok = False
else:
    print("[=] vmcu shell уже есть")

# 6) tests
if 'VMCU test:' not in src:
    old = '''    std::printf("=== Готово ===\\n\\n");
    return 0;
}'''
    new = '''    // VMCU int
    {
        Emulator emu;
        uint32_t prog[] = {0x1000002A, 0x11000008, 0x43000000};
        for (int i = 0; i < 3; i++)
            for (int b = 0; b < 4; b++)
                emu.vmcu.mem[i*4+b] = (prog[i] >> (b*8)) & 0xFF;
        emu.vmcu.pc = 0; emu.vmcu.running = true; emu.vmcu.halted = false;
        for (int i = 0; i < 200 && !emu.vmcu.halted; i++) emu.vmcu.step();
        std::printf("VMCU test: r0=%u halted=%d cycles=%llu (ожид. 50, 1, 3)\\n",
                    emu.vmcu.r[0], (int)emu.vmcu.halted,
                    (unsigned long long)emu.vmcu.cycles);
    }

    // VMCU FPU
    {
        Emulator emu;
        uint32_t prog[] = {0x5D000005, 0x5D100003, 0x50201000, 0x5A020000, 0x43000000};
        for (int i = 0; i < 5; i++)
            for (int b = 0; b < 4; b++)
                emu.vmcu.mem[i*4+b] = (prog[i] >> (b*8)) & 0xFF;
        emu.vmcu.pc = 0; emu.vmcu.running = true; emu.vmcu.halted = false;
        for (int i = 0; i < 200 && !emu.vmcu.halted; i++) emu.vmcu.step();
        std::printf("VMCU FPU: r0=%u F2=%.6g halted=%d (ожид. 8, 8, 1)\\n",
                    emu.vmcu.r[0], (double)emu.vmcu.fp[2], (int)emu.vmcu.halted);
    }

    std::printf("=== Готово ===\\n\\n");
    return 0;
}'''
    src = sub_once(src, old, new, "run_tests: VMCU + FPU")
else:
    print("[=] VMCU тест уже есть")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО — что-то не применилось")
