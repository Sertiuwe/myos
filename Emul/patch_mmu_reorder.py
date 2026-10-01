import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

# 1) Найдём блок mmu map
start = src.find('        if (std::strcmp(cmd, "mmu") == 0 && arg1[0] == \'m\' && arg1[1] == \'a\' && arg1[2] == \'p\') {')
end   = src.find('        if (std::strcmp(cmd, "tlb") == 0) {', start)
if start < 0 or end < 0:
    print("[!] не найден mmu map")
    raise SystemExit(1)
block = src[start:end]

# 2) Удаляем его из текущего места
src = src[:start] + src[end:]

# 3) Вставляем ПЕРЕД общим блоком mmu
anchor = '        if (std::strcmp(cmd, "mmu") == 0) {'
if anchor not in src:
    print("[!] не найден общий mmu")
    raise SystemExit(1)
src = src.replace(anchor, block + anchor, 1)

# 4) Также добавим "map" как алиас в общий mmu, на случай если что
old = '''                } else {
                    std::printf("mmu on|off|flush|setup [ttbr]|dump|tlb\\n");
                }'''
new = '''                } else {
                    std::printf("mmu on|off|flush|setup [ttbr]|map <v> <p> [kb]|dump|tlb\\n");
                }'''
src = src.replace(old, new, 1)

p.write_text(src)
print("[+] mmu map перемещён выше общего mmu")
