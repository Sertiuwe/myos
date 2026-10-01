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

# 1. Include
src = sub_once(src, '#include "vmcu.h"\n',
              '#include "vmcu.h"\n#include "virtlcd.h"\n',
              "include virtlcd.h")

# 2. Member in Emulator
src = sub_once(src,
    '    VMCU       vmcu{0x10110000, "VMCU"};\n',
    '    VMCU       vmcu{0x10110000, "VMCU"};\n'
    '    VirtLCD    lcd {0x10120000, "LCD"};\n',
    "member lcd")

# 3. Attach in rebind()
src = sub_once(src,
    '        vmcu.attach_shared(mem.data.data() + 0x70000);\n'
    '        vmcu.attach_arm4(mem.data.data(), MEM_SIZE);',
    '        vmcu.attach_shared(mem.data.data() + 0x70000);\n'
    '        vmcu.attach_arm4(mem.data.data(), MEM_SIZE);\n'
    '        lcd.attach_mem(mem.data.data(), MEM_SIZE);',
    "attach lcd mem")

# 4. MMIO routing
pairs = [
 ("if (emu->vmcu.contains(a))   return emu->vmcu.read(a, 4);",
  "if (emu->lcd.contains(a))    return emu->lcd.read(a, 4);", "read32"),
 ("if (emu->vmcu.contains(a))   return (uint16_t)emu->vmcu.read(a, 2);",
  "if (emu->lcd.contains(a))    return (uint16_t)emu->lcd.read(a, 2);", "read16"),
 ("if (emu->vmcu.contains(a))   return (uint8_t)emu->vmcu.read(a, 1);",
  "if (emu->lcd.contains(a))    return (uint8_t)emu->lcd.read(a, 1);", "read8"),
 ("if (emu->vmcu.contains(a))   { emu->vmcu.write(a, v, 4); return; }",
  "if (emu->lcd.contains(a))    { emu->lcd.write(a, v, 4); return; }", "write32"),
 ("if (emu->vmcu.contains(a))   { emu->vmcu.write(a, v, 2); return; }",
  "if (emu->lcd.contains(a))    { emu->lcd.write(a, v, 2); return; }", "write16"),
 ("if (emu->vmcu.contains(a))   { emu->vmcu.write(a, v, 1); return; }",
  "if (emu->lcd.contains(a))    { emu->lcd.write(a, v, 1); return; }", "write8"),
]
for old, tail, tag in pairs:
    if tail in src:
        print(f"[=] уже есть: {tag}"); continue
    if old not in src:
        print(f"[!] НЕТ: {tag}"); ok = False; continue
    src = src.replace(old, old + "\n        " + tail, 1)
    print(f"[+] OK: {tag}")

# 5. tick + IRQ source 17
src = sub_once(src,
    '        vmcu.tick();\n'
    '        vic.set_source(16, vmcu.irq_pending());',
    '        vmcu.tick();\n'
    '        lcd.tick();\n'
    '        vic.set_source(16, vmcu.irq_pending());\n'
    '        vic.set_source(17, lcd.irq_pending());',
    "tick lcd + IRQ 17")

# 6. Shell commands
if 'lcd dump' not in src:
    cmds = r'''        if (std::strcmp(cmd, "lcd") == 0) {
            VirtLCD& L = emu.lcd;
            if (!arg1[0] || std::strcmp(arg1, "dump") == 0) { L.dump(); return false; }
            if (std::strcmp(arg1, "on") == 0) {
                L.init_term();
                L.control |= VirtLCD::CNTL_LCDEN;
                std::printf("LCD on\n");
                return false;
            }
            if (std::strcmp(arg1, "off") == 0) {
                L.control &= ~VirtLCD::CNTL_LCDEN;
                L.shutdown();
                std::printf("LCD off\n");
                return false;
            }
            if (std::strcmp(arg1, "render") == 0) {
                L.render();
                std::printf("LCD rendered\n");
                return false;
            }
            if (std::strcmp(arg1, "fb") == 0) {
                /* lcd fb <addr> : установить framebuffer base */
                if (!arg2[0]) { std::printf("lcd fb <addr>\n"); return false; }
                L.upbase = parse_u32(arg2) & 0xFFFFFFF0u;
                L.control |= VirtLCD::CNTL_LCDEN | VirtLCD::CNTL_LCDBPP_16 | VirtLCD::CNTL_LCDTFT;
                L.init_term();
                std::printf("LCD FB=0x%08X, 320x240 RGB565\n", L.upbase);
                return false;
            }
            if (std::strcmp(arg1, "fill") == 0) {
                /* lcd fill <rgb hex> : залить framebuffer цветом (RGB565) */
                if (!arg2[0]) { std::printf("lcd fill <rgb565>\n"); return false; }
                if (!L.upbase) { std::printf("lcd fb <addr> сначала\n"); return false; }
                uint16_t c = (uint16_t)parse_u32(arg2);
                size_t row_bytes = 320 * 2;
                for (int y = 0; y < 240; y++)
                    for (int x = 0; x < 320; x++)
                        std::memcpy(emu.mem.data.data() + L.upbase + y*row_bytes + x*2, &c, 2);
                L.render();
                std::printf("LCD fill 0x%04X\n", c);
                return false;
            }
            if (std::strcmp(arg1, "pixel") == 0) {
                /* lcd pixel <x> <y> <rgb565> */
                char xs[16]={0}, ys[16]={0}, cs[16]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %15s %15s %15s", xs, ys, cs);
                if (!xs[0] || !ys[0] || !cs[0]) { std::printf("lcd pixel <x> <y> <rgb565>\n"); return false; }
                int x = std::atoi(xs), y = std::atoi(ys);
                uint16_t c = (uint16_t)parse_u32(cs);
                if (!L.upbase || x < 0 || x >= 320 || y < 0 || y >= 240) return false;
                std::memcpy(emu.mem.data.data() + L.upbase + y*640 + x*2, &c, 2);
                return false;
            }
            std::printf("lcd dump|on|off|render|fb <addr>|fill <rgb565>|pixel <x> <y> <rgb565>\n");
            return false;
        }
'''
    anchor = '        if (std::strcmp(cmd, "uart0") == 0) {\n'
    if anchor in src:
        src = src.replace(anchor, cmds + anchor, 1)
        print("[+] OK: shell lcd")
    else:
        print("[!] не найден anchor uart0"); ok = False

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО")
