import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()

def sub_once(s, old, new, tag):
    """Возвращает (src, ok)."""
    if old not in s:
        print(f"[!] нет: {tag}")
        return s, False
    if s.count(old) != 1:
        print(f"[!] неоднозначно ({s.count(old)}): {tag}")
        return s, False
    print(f"[+] {tag}")
    return s.replace(old, new, 1), True

ok = True

# read16 -> dcache
if 'return dcache.read16(a);' not in src:
    old = '''    if (!valid16(a)) { flag_fault(a, false); return 0; }
    uint16_t v; std::memcpy(&v, &data[a], 2); return v;
}
uint8_t Memory::read8(uint32_t a) {'''
    new = '''    if (!valid16(a)) { flag_fault(a, false); return 0; }
    return dcache.read16(a);
}
uint8_t Memory::read8(uint32_t a) {'''
    src, r = sub_once(src, old, new, 'read16 -> dcache'); ok &= r
else:
    print("[=] read16 уже ok")

# read8 -> dcache
if 'return dcache.read8(a);' not in src:
    old = '''    if (!valid8(a)) { flag_fault(a, false); return 0; }
    return data[a];
}
'''
    new = '''    if (!valid8(a)) { flag_fault(a, false); return 0; }
    return dcache.read8(a);
}
'''
    src, r = sub_once(src, old, new, 'read8 -> dcache'); ok &= r
else:
    print("[=] read8 уже ok")

# write32 -> dcache
if 'dcache.write32(a, v);' not in src:
    old = '''    if (!valid32(a)) { flag_fault(a, true); return; }
    std::memcpy(&data[a], &v, 4);
}
void Memory::write16'''
    new = '''    if (!valid32(a)) { flag_fault(a, true); return; }
    dcache.write32(a, v);
}
void Memory::write16'''
    src, r = sub_once(src, old, new, 'write32 -> dcache'); ok &= r
else:
    print("[=] write32 уже ok")

# write16 -> dcache
if 'dcache.write16(a, v);' not in src:
    old = '''    if (!valid16(a)) { flag_fault(a, true); return; }
    std::memcpy(&data[a], &v, 2);
}
void Memory::write8'''
    new = '''    if (!valid16(a)) { flag_fault(a, true); return; }
    dcache.write16(a, v);
}
void Memory::write8'''
    src, r = sub_once(src, old, new, 'write16 -> dcache'); ok &= r
else:
    print("[=] write16 уже ok")

# write8 -> dcache
if 'dcache.write8(a, v);' not in src:
    old = '''    if (!valid8(a)) { flag_fault(a, true); return; }
    data[a] = v;
}
'''
    new = '''    if (!valid8(a)) { flag_fault(a, true); return; }
    dcache.write8(a, v);
}
'''
    src, r = sub_once(src, old, new, 'write8 -> dcache'); ok &= r
else:
    print("[=] write8 уже ok")

# read32_ic / read16_ic
if 'uint32_t Memory::read32_ic' not in src:
    old = 'uint8_t Memory::read8(uint32_t a) {'
    new = '''uint32_t Memory::read32_ic(uint32_t a) {
    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return icache.read32(a);
}
uint16_t Memory::read16_ic(uint32_t a) {
    if (!valid16(a)) { flag_fault(a, false); return 0; }
    return icache.read16(a);
}
uint8_t Memory::read8(uint32_t a) {'''
    src, r = sub_once(src, old, new, 'impl read32_ic/16_ic'); ok &= r
else:
    print("[=] read32_ic уже ok")

# fetch -> I-cache
if 'mem.read32_ic(pc);' not in src:
    old = '''    uint32_t fetch_arm() {
        uint32_t pc = cpu.r[15];
        cpu.r[15] = pc + 4;
        return mem.read32(pc);
    }
    uint16_t fetch_thumb() {
        uint32_t pc = cpu.r[15];
        cpu.r[15] = pc + 2;
        return mem.read16(pc);
    }'''
    new = '''    uint32_t fetch_arm() {
        uint32_t pc = cpu.r[15];
        cpu.r[15] = pc + 4;
        return mem.read32_ic(pc);
    }
    uint16_t fetch_thumb() {
        uint32_t pc = cpu.r[15];
        cpu.r[15] = pc + 2;
        return mem.read16_ic(pc);
    }'''
    src, r = sub_once(src, old, new, 'fetch -> I-cache'); ok &= r
else:
    print("[=] fetch уже ok")

# команды cache
if '"cache dump' not in src and 'cache dump|on' not in src:
    cmds = r'''        if (std::strcmp(cmd, "cache") == 0) {
            if (!arg1[0] || std::strcmp(arg1, "dump") == 0 || std::strcmp(arg1, "stats") == 0) {
                emu.mem.icache.dump();
                emu.mem.dcache.dump();
                return false;
            }
            if (std::strcmp(arg1, "on") == 0) {
                emu.mem.icache.enabled = true;
                emu.mem.dcache.enabled = true;
                std::printf("cache on\n"); return false;
            }
            if (std::strcmp(arg1, "off") == 0) {
                emu.mem.icache.enabled = false;
                emu.mem.dcache.enabled = false;
                std::printf("cache off\n"); return false;
            }
            if (std::strcmp(arg1, "flush") == 0 || std::strcmp(arg1, "invalidate") == 0) {
                emu.mem.icache.invalidate_all();
                emu.mem.dcache.invalidate_all();
                std::printf("cache flushed\n"); return false;
            }
            if (std::strcmp(arg1, "reset") == 0) {
                emu.mem.icache.reset_stats();
                emu.mem.dcache.reset_stats();
                std::printf("cache stats reset\n"); return false;
            }
            std::printf("cache dump|on|off|flush|reset\n");
            return false;
        }
'''
    anchor = '        if (std::strcmp(cmd, "net") == 0) {\n'
    if anchor in src:
        src = src.replace(anchor, cmds + anchor, 1)
        print("[+] команды cache")
    else:
        print("[!] anchor net не найден"); ok = False
else:
    print("[=] команды cache уже есть")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
