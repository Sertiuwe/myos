import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''            if (std::strcmp(arg1, "loopback") == 0) {'''

new = '''            if (std::strcmp(arg1, "irq") == 0) {
                if (!arg2[0]) {
                    std::printf("irq_stat=0x%X irq_mask=0x%X\\n",
                                emu.net.irq_stat, emu.net.irq_mask);
                    return false;
                }
                emu.net.irq_mask = parse_u32(arg2);
                std::printf("irq_mask = 0x%X\\n", emu.net.irq_mask);
                return false;
            }
            if (std::strcmp(arg1, "clrirq") == 0) {
                emu.net.irq_stat = 0;
                std::printf("irq_stat очищен\\n");
                return false;
            }
            if (std::strcmp(arg1, "loopback") == 0) {'''

if old not in src:
    print("[!] шаблон не найден")
else:
    src = src.replace(old, new, 1)
    print("[+] подкоманды net irq / net clrirq добавлены")

p.write_text(src)
