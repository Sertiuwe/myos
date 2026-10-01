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

old = '''                } else if (std::strcmp(arg1, "reset") == 0) {
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
new = '''                } else if (std::strcmp(arg1, "reset") == 0) {
                    emu.cp15 = CP15();
                    emu.mmu.flush();
                    emu.cp15.icache_ptr = &emu.mem.icache;
                    emu.cp15.dcache_ptr = &emu.mem.dcache;
                    std::printf("CP15 + MMU reset\\n");
                } else if (std::strcmp(arg1, "stats") == 0) {
                    emu.mmu.dump();
                } else if (std::strcmp(arg1, "dump") == 0) {
                    emu.mmu.dump();
                    emu.cp15.dump();
                } else if (std::strcmp(arg1, "tlb") == 0) {
                    emu.mmu.dump_tlb();
                } else {
                    std::printf("mmu on|off|flush|reset|setup [ttbr]|map <v> <p> [kb]|dump|stats|tlb\\n");
                }'''
src = sub_once(src, old, new, "mmu: dump/tlb branches")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО")
