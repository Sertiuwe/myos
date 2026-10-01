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

# ---------------------------------------------------------------
# 1. operand2_shift: чтение R15 как операнда = I+8 (было I+4)
# ---------------------------------------------------------------
old = r'''        uint32_t rm = instr & 0xF;
        uint32_t type = (instr >> 5) & 0x3;
        uint32_t shift = (instr >> 7) & 0x1F;
        uint32_t v = cpu.get_reg(rm);'''
new = r'''        uint32_t rm = instr & 0xF;
        uint32_t type = (instr >> 5) & 0x3;
        uint32_t shift = (instr >> 7) & 0x1F;
        // ARM ARM: R15 as shift operand reads PC = I+8 (r[15] already = I+4)
        uint32_t v = (rm == 15) ? (cpu.r[15] + 4) : cpu.get_reg(rm);'''
src = sub_once(src, old, new, "operand2_shift: R15=PC")

# ---------------------------------------------------------------
# 2. MRS из User/System -> Undefined (нет SPSR)
# ---------------------------------------------------------------
old = r'''    void exec_mrs(uint32_t instr) {
        bool r = (instr >> 22) & 1;
        uint32_t rd = (instr >> 12) & 0xF;
        uint32_t v = r ? cpu.banks[cpu.bank_idx()].spsr : cpu.cpsr.raw;'''
new = r'''    void exec_mrs(uint32_t instr) {
        bool r = (instr >> 22) & 1;
        uint32_t rd = (instr >> 12) & 0xF;
        if (r && (cpu.cpsr.mode() == Mode::User || cpu.cpsr.mode() == Mode::System)) {
            cpu.take_exception(0x04, Mode::Undefined, false);
            return;
        }
        uint32_t v = r ? cpu.banks[cpu.bank_idx()].spsr : cpu.cpsr.raw;'''
src = sub_once(src, old, new, "MRS: User/System -> undef")

# ---------------------------------------------------------------
# 3. MSR в User/System -> Undefined
# ---------------------------------------------------------------
old = r'''    void exec_msr(uint32_t instr) {
        bool r = (instr >> 22) & 1;
        uint32_t field_mask = (instr >> 16) & 0xF;'''
new = r'''    void exec_msr(uint32_t instr) {
        bool r = (instr >> 22) & 1;
        if (r && (cpu.cpsr.mode() == Mode::User || cpu.cpsr.mode() == Mode::System)) {
            cpu.take_exception(0x04, Mode::Undefined, false);
            return;
        }
        uint32_t field_mask = (instr >> 16) & 0xF;'''
src = sub_once(src, old, new, "MSR: User/System -> undef")

p.write_text(src)
print("P1 РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО")
