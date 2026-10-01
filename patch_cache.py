import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()
ok = True

def sub_once(s, old, new, tag):
    global ok
    if old not in s:
        print(f"[!] нет: {tag}"); ok = False; return s
    if s.count(old) != 1:
        print(f"[!] неоднозначно ({s.count(old)}): {tag}"); ok = False; return s
    print(f"[+] {tag}")
    return s.replace(old, new, 1)

# 1) include
if '#include "cache.h"' not in src:
    src = sub_once(src, '#include "virtnet.h"\n',
                   '#include "virtnet.h"\n#include "cache.h"\n',
                   'include cache.h')

# 2) добавить кеши в Memory
old = '''class Memory {
public:
    std::vector<uint8_t> data;
'''
new = '''class Memory {
public:
    std::vector<uint8_t> data;

    Cache icache{"I"};
    Cache dcache{"D"};
'''
if 'Cache icache{"I"};' not in src:
    src = sub_once(src, old, new, 'кеши в Memory')

# 3) attach в конструкторе
old = '''    Memory() : data(MEM_SIZE, 0) {}
'''
new = '''    Memory() : data(MEM_SIZE, 0) {
        icache.attach(data.data(), MEM_SIZE);
        dcache.attach(data.data(), MEM_SIZE);
    }
'''
if 'icache.attach(' not in src:
    src = sub_once(src, old, new, 'attach в конструкторе')

# 4) добавить read32_ic / read16_ic в объявление
old = '''    uint32_t read32(uint32_t a);
    uint16_t read16(uint32_t a);
    uint8_t  read8 (uint32_t a);
'''
new = '''    uint32_t read32(uint32_t a);
    uint16_t read16(uint32_t a);
    uint8_t  read8 (uint32_t a);
    uint32_t read32_ic(uint32_t a);
    uint16_t read16_ic(uint32_t a);
'''
if 'read32_ic' not in src:
    src = sub_once(src, old, new, 'объявления read32_ic')

# 5) маршрутизация в реализациях read32/16/8
old = '''    if (!valid32(a)) { flag_fault(a, false); return 0; }
    uint32_t v; std::memcpy(&v, &data[a], 4); return v;
}
uint16_t Memory::read16(uint32_t a) {'''
new = '''    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return dcache.read32(a);
}
uint16_t Memory::read16(uint32_t a) {'''
src, r = sub_once(src, old, new, 'read32 -> dcache')
ok &= r

old = '''    if (!valid16(a)) { flag_fault(a, false); return 0; }
    uint16_t v; std::memcpy(&v, &data[a], 2); return v;
}
uint8_t Memory::read8(uint32_t a) {'''
new = '''    if (!valid16(a)) { flag_fault(a, false); return 0; }
    return dcache.read16(a);
}
uint8_t Memory::read8(uint32_t a) {'''
src, r = sub_once(src, old, new, 'read16 -> dcache')
ok &= r

old = '''    if (!valid8(a)) { flag_fault(a, false); return 0; }
    return data[a];
}
'''
new = '''    if (!valid8(a)) { flag_fault(a, false); return 0; }
    return dcache.read8(a);
}
'''
src, r = sub_once(src, old, new, 'read8 -> dcache')
ok &= r

# 6) write32/16/8 через dcache
old = '''    if (!valid32(a)) { flag_fault(a, true); return; }
    std::memcpy(&data[a], &v, 4);
}
void Memory::write16'''
new = '''    if (!valid32(a)) { flag_fault(a, true); return; }
    dcache.write32(a, v);
}
void Memory::write16'''
src, r = sub_once(src, old, new, 'write32 -> dcache')
ok &= r

old = '''    if (!valid16(a)) { flag_fault(a, true); return; }
    std::memcpy(&data[a], &v, 2);
}
void Memory::write8'''
new = '''    if (!valid16(a)) { flag_fault(a, true); return; }
    dcache.write16(a, v);
}
void Memory::write8'''
src, r = sub_once(src, old, new, 'write16 -> dcache')
ok &= r

old = '''    if (!valid8(a)) { flag_fault(a, true); return; }
    data[a] = v;
}
'''
new = '''    if (!valid8(a)) { flag_fault(a, true); return; }
    dcache.write8(a, v);
}
'''
src, r = sub_once(src, old, new, 'write8 -> dcache')
ok &= r

# 7) read32_ic / read16_ic — вставляем после read8 impl
old = '''uint8_t Memory::read8(uint32_t a) {'''
new = '''uint32_t Memory::read32_ic(uint32_t a) {
    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return icache.read32(a);
}
uint16_t Memory::read16_ic(uint32_t a) {
    if (!valid16(a)) { flag_fault(a, false); return 0; }
    return icache.read16(a);
}
uint8_t Memory::read8(uint32_t a) {'''
src, r = sub_once(src, old, new, 'impl read32_ic/16_ic')
ok &= r

# 8) fetch через I-cache
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
src, r = sub_once(src, old, new, 'fetch_arm/thumb -> I-cache')
ok &= r

# 9) команды cache
if '"cache flush' not in src:
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

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
