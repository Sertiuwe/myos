import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()
ok = True

def sub(s, old, new, tag):
    global ok
    if old not in s:
        print(f"[!] нет: {tag}"); ok = False; return s
    if s.count(old) != 1:
        print(f"[!] неоднозначно ({s.count(old)}): {tag}"); ok = False; return s
    print(f"[+] {tag}")
    return s.replace(old, new, 1)

# 1) include mmu.h
if '#include "mmu.h"' not in src:
    src = sub(src, '#include "cache.h"\n',
              '#include "cache.h"\n#include "mmu.h"\n', 'include mmu.h')

# 2) Bank struct — добавить R8-R12 для FIQ
old = 'struct Bank { uint32_t r13 = 0; uint32_t r14 = 0; uint32_t spsr = 0; };'
new = ('struct Bank { uint32_t r8_12[5] = {0}; '
       'uint32_t r13 = 0; uint32_t r14 = 0; uint32_t spsr = 0; };')
if 'r8_12[5]' not in src:
    src = sub(src, old, new, 'Bank + R8-R12')

# 3) get_reg/set_reg — FIQ banked R8-R12
old = '''    uint32_t get_reg(int n) const {
        if (n == 15) return r[15];
        if (n == 13 || n == 14) {
            int idx = bank_idx();
            if (idx == 0 || idx == 6) return r[n];
            const Bank& b = banks[idx];
            return (n == 13) ? b.r13 : b.r14;
        }
        return r[n];
    }'''
new = '''    uint32_t get_reg(int n) const {
        if (n == 15) return r[15];
        int idx = bank_idx();
        if (n >= 8 && n <= 12) {
            if (idx == 1) return banks[1].r8_12[n - 8];
            return r[n];
        }
        if (n == 13 || n == 14) {
            if (idx == 0 || idx == 6) return r[n];
            const Bank& b = banks[idx];
            return (n == 13) ? b.r13 : b.r14;
        }
        return r[n];
    }'''
if 'banks[1].r8_12' not in src:
    src = sub(src, old, new, 'get_reg FIQ')

old = '''    void set_reg(int n, uint32_t v) {
        if (n == 15) { r[15] = v; return; }
        if (n == 13 || n == 14) {
            int idx = bank_idx();
            if (idx == 0 || idx == 6) { r[n] = v; return; }
            Bank& b = banks[idx];
            if (n == 13) b.r13 = v; else b.r14 = v;
            return;
        }
        r[n] = v;
    }'''
new = '''    void set_reg(int n, uint32_t v) {
        if (n == 15) { r[15] = v; return; }
        int idx = bank_idx();
        if (n >= 8 && n <= 12) {
            if (idx == 1) { banks[1].r8_12[n - 8] = v; return; }
            r[n] = v; return;
        }
        if (n == 13 || n == 14) {
            if (idx == 0 || idx == 6) { r[n] = v; return; }
            Bank& b = banks[idx];
            if (n == 13) b.r13 = v; else b.r14 = v;
            return;
        }
        r[n] = v;
    }'''
if 'banks[1].r8_12[n - 8] = v' not in src:
    src = sub(src, old, new, 'set_reg FIQ')

# 4) cp15 + mmu как члены Emulator
if 'CP15       cp15;' not in src:
    old = '    VirtBuf    buf{0x10017000, "BUF0"};\n'
    new = ('    VirtBuf    buf{0x10017000, "BUF0"};\n'
           '    CP15       cp15;\n'
           '    MMU        mmu;\n')
    src = sub(src, old, new, 'cp15+mmu в Emulator')

# 5) attach mmu в конструкторе Emulator
old = '''    Emulator() {
        mem.ports = &ports;
        mem.emu = this;
        g_cpu_ptr = &cpu;
    }'''
new = '''    Emulator() {
        mem.ports = &ports;
        mem.emu = this;
        g_cpu_ptr = &cpu;
        mmu.attach(mem.data.data(), MEM_SIZE);
    }'''
if 'mmu.attach(mem.data' not in src:
    src = sub(src, old, new, 'attach mmu')

# 6) exec_coprocessor — CP15
old = '''    void exec_coprocessor(uint32_t instr) {
        uint32_t cp = (instr >> 8) & 0xF;
        bool l = (instr >> 20) & 1;
        uint32_t rd = (instr >> 12) & 0xF;
        if ((instr & 0x0F000000) == 0x0E000000) {
            if (cp == 15 && l) cpu.set_reg(rd, 0);
            return;
        }
        cpu.take_exception(0x04, Mode::Undefined, false);
    }'''
new = '''    void exec_coprocessor(uint32_t instr) {
        uint32_t cp = (instr >> 8) & 0xF;
        bool l = (instr >> 20) & 1;
        uint32_t rd = (instr >> 12) & 0xF;
        bool bit4 = (instr >> 4) & 1;

        // MCR/MRC (bits 27-24=1110, bit4=1)
        if ((instr & 0x0F000000) == 0x0E000000 && bit4) {
            if (cp == 15) {
                uint32_t op1 = (instr >> 21) & 0x7;
                uint32_t crn = (instr >> 16) & 0xF;
                uint32_t op2 = (instr >> 5)  & 0x7;
                uint32_t crm =  instr        & 0xF;
                if (l) {
                    cpu.set_reg(rd, cp15.read(op1, crn, crm, op2));
                } else {
                    cp15.write(op1, crn, crm, op2, cpu.get_reg(rd));
                }
            }
            return;
        }
        // CDP — не поддерживаем
        cpu.take_exception(0x04, Mode::Undefined, false);
    }'''
if 'cp15.read(op1' not in src:
    src = sub(src, old, new, 'CP15 в exec_coprocessor')

# 7) Memory: трансляция через MMU. Патчим все 6 функций.
# read32
old = '''    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return dcache.read32(a);
}
uint16_t Memory::read16(uint32_t a) {'''
new = '''    if (!valid32(a)) { flag_fault(a, false); return 0; }
    return dcache.read32(a);
}
uint16_t Memory::read16(uint32_t a) {'''
# пропускаем, пока не нужно — трансляция делается выше

# Функция mmu_translate в Memory (inline) + вызов в начале каждой
old = '''uint32_t Memory::read32(uint32_t a) {
    if (ports && PortManager::is_port(a)) return ports->read(a, 4);'''
new = '''static inline uint32_t mmu_xlate(Memory* m, uint32_t a, bool write, bool& fault) {
    fault = false;
    if (!m->emu || !m->emu->cp15.mmu_on()) return a;
    uint32_t pa = m->emu->mmu.translate(a, write, fault, m->emu->cp15);
    return pa;
}

uint32_t Memory::read32(uint32_t a) {
    if (ports && PortManager::is_port(a)) return ports->read(a, 4);
    {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, false, f);
        if (f) { flag_fault(a, false); return 0; }
        a = pa;
    }'''
if 'mmu_xlate' not in src:
    src = sub(src, old, new, 'read32 MMU')

old = '''uint16_t Memory::read16(uint32_t a) {
    if (ports && PortManager::is_port(a)) return (uint16_t)ports->read(a, 2);'''
new = '''uint16_t Memory::read16(uint32_t a) {
    if (ports && PortManager::is_port(a)) return (uint16_t)ports->read(a, 2);
    {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, false, f);
        if (f) { flag_fault(a, false); return 0; }
        a = pa;
    }'''
if 'read16(uint32_t a)' in src and src.count('mmu_xlate') < 3:
    src = sub(src, old, new, 'read16 MMU')

old = '''uint8_t Memory::read8(uint32_t a) {
    if (ports && PortManager::is_port(a)) return (uint8_t)ports->read(a, 1);'''
new = '''uint8_t Memory::read8(uint32_t a) {
    if (ports && PortManager::is_port(a)) return (uint8_t)ports->read(a, 1);
    {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, false, f);
        if (f) { flag_fault(a, false); return 0; }
        a = pa;
    }'''
if src.count('mmu_xlate') < 4:
    src = sub(src, old, new, 'read8 MMU')

old = '''void Memory::write32(uint32_t a, uint32_t v) {
    if (ports && PortManager::is_port(a)) { ports->write(a, v, 4); return; }'''
new = '''void Memory::write32(uint32_t a, uint32_t v) {
    if (ports && PortManager::is_port(a)) { ports->write(a, v, 4); return; }
    {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, true, f);
        if (f) { flag_fault(a, true); return; }
        a = pa;
    }'''
if src.count('mmu_xlate') < 5:
    src = sub(src, old, new, 'write32 MMU')

old = '''void Memory::write16(uint32_t a, uint16_t v) {
    if (ports && PortManager::is_port(a)) { ports->write(a, v, 2); return; }'''
new = '''void Memory::write16(uint32_t a, uint16_t v) {
    if (ports && PortManager::is_port(a)) { ports->write(a, v, 2); return; }
    {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, true, f);
        if (f) { flag_fault(a, true); return; }
        a = pa;
    }'''
if src.count('mmu_xlate') < 6:
    src = sub(src, old, new, 'write16 MMU')

old = '''void Memory::write8(uint32_t a, uint8_t v) {
    if (ports && PortManager::is_port(a)) { ports->write(a, v, 1); return; }'''
new = '''void Memory::write8(uint32_t a, uint8_t v) {
    if (ports && PortManager::is_port(a)) { ports->write(a, v, 1); return; }
    {
        bool f = false;
        uint32_t pa = mmu_xlate(this, a, true, f);
        if (f) { flag_fault(a, true); return; }
        a = pa;
    }'''
if src.count('mmu_xlate') < 7:
    src = sub(src, old, new, 'write8 MMU')

# 8) Баннер v18
old = '    std::printf("|     ARM4 + Thumb Emulator (v17)              |\\n");'
new = '    std::printf("|     ARM4 + Thumb Emulator (v18)              |\\n");'
if 'v18' not in src.split('print_banner')[1][:500]:
    src = sub(src, old, new, 'banner v18')
src = src.replace('|     PL190 VIC + PL011/SP804/PL061            |\\n',
                  '|     PL190 VIC + PL011/SP804/PL061 + MMU      |\\n')

# 9) команды cp15/mmu/tlb
if '"cp15"' not in src:
    cmds = r'''        if (std::strcmp(cmd, "cp15") == 0) {
            emu.cp15.dump();
            return false;
        }
        if (std::strcmp(cmd, "mmu") == 0) {
            if (arg1[0]) {
                if (std::strcmp(arg1, "off") == 0) {
                    emu.cp15.sctlr &= ~1u;
                    std::printf("MMU off\n");
                } else if (std::strcmp(arg1, "on") == 0) {
                    emu.cp15.sctlr |= 1u;
                    std::printf("MMU on\n");
                } else if (std::strcmp(arg1, "flush") == 0) {
                    emu.mmu.flush();
                    std::printf("TLB flushed\n");
                } else {
                    std::printf("mmu on|off|flush|dump|tlb\n");
                }
                return false;
            }
            emu.mmu.dump();
            return false;
        }
        if (std::strcmp(cmd, "tlb") == 0) {
            if (arg1[0] && std::strcmp(arg1, "flush") == 0) {
                emu.mmu.flush();
                std::printf("TLB flushed\n");
            } else {
                emu.mmu.dump_tlb();
            }
            return false;
        }
        if (std::strcmp(cmd, "v18") == 0) {
            std::printf("ARM4 v18 — CP15 + MMU + FIQ R8-R12\n");
            return false;
        }
'''
    anchor = '        if (std::strcmp(cmd, "cache") == 0) {\n'
    if anchor in src:
        src = src.replace(anchor, cmds + anchor, 1)
        print("[+] команды cp15/mmu/tlb")
    else:
        print("[!] anchor cache не найден"); ok = False

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
