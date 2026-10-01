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
# 1. Thumb data abort LR = I+8 (было I+4) + добавляем prefetch abort
# ---------------------------------------------------------------
old = r'''    void take_data_abort() {
        int idx = mode_index(Mode::Abort);
        banks[idx].spsr = cpsr.raw;
        uint32_t off = cpsr.t() ? 2u : 4u;
        banks[idx].r14 = r[15] + off;
        cpsr.set_mode(Mode::Abort);
        cpsr.set_t(false);
        cpsr.set_i(true);
        r[15] = 0x10;
    }'''
new = r'''    void take_data_abort() {
        int idx = mode_index(Mode::Abort);
        banks[idx].spsr = cpsr.raw;
        // ARM ARM: LR_abt = faulting_PC + 8 in both ARM and Thumb.
        // r[15] already advanced by fetch: I+4 (ARM) or I+2 (Thumb).
        uint32_t off = cpsr.t() ? 6u : 4u;
        banks[idx].r14 = r[15] + off;
        cpsr.set_mode(Mode::Abort);
        cpsr.set_t(false);
        cpsr.set_i(true);
        r[15] = 0x10;
    }

    void take_prefetch_abort() {
        int idx = mode_index(Mode::Abort);
        banks[idx].spsr = cpsr.raw;
        // ARM ARM: LR_abt = faulting_PC + 4.
        // r[15] already advanced by fetch: I+4 (ARM) or I+2 (Thumb).
        uint32_t off = cpsr.t() ? 2u : 0u;
        banks[idx].r14 = r[15] + off;
        cpsr.set_mode(Mode::Abort);
        cpsr.set_t(false);
        cpsr.set_i(true);
        r[15] = 0x0C;
    }'''
src = sub_once(src, old, new, "take_data_abort(+prefetch)")

# ---------------------------------------------------------------
# 2. Thumb SP: 4 места в exec_thumb завязаны на banks[3].r13
# ---------------------------------------------------------------
old = r'''            uint32_t sp = cpu.banks[3].r13;
            if (l) cpu.set_reg(rd, mem.read32(sp + imm));
            else   mem.write32(sp + imm, cpu.get_reg(rd));'''
new = r'''            uint32_t sp = cpu.get_reg(13);
            if (l) cpu.set_reg(rd, mem.read32(sp + imm));
            else   mem.write32(sp + imm, cpu.get_reg(rd));'''
src = sub_once(src, old, new, "Thumb SP: LDR/STR [SP]")

old = r'''            cpu.set_reg(rd, sp ? (cpu.banks[3].r13 + imm) : (pc + imm));'''
new = r'''            cpu.set_reg(rd, sp ? (cpu.get_reg(13) + imm) : (pc + imm));'''
src = sub_once(src, old, new, "Thumb SP: ADD Rd,SP/PC")

old = r'''            if (sub) cpu.banks[3].r13 -= imm;
            else     cpu.banks[3].r13 += imm;'''
new = r'''            uint32_t sp = cpu.get_reg(13);
            if (sub) cpu.set_reg(13, sp - imm);
            else     cpu.set_reg(13, sp + imm);'''
src = sub_once(src, old, new, "Thumb SP: ADD/SUB SP")

old = r'''            uint32_t sp = cpu.banks[3].r13;
            if (l) {
                for (int i = 0; i < 8; i++) {
                    if (reglist & (1 << i)) {
                        cpu.set_reg(i, mem.read32(sp & ~3u));
                        sp += 4;
                    }
                }
                if (r) {
                    uint32_t pc = mem.read32(sp & ~3u);
                    sp += 4;
                    cpu.r[15] = pc & ~1u;
                }
                cpu.banks[3].r13 = sp;
            } else {
                int cnt = 0;
                for (int i = 0; i < 8; i++) if (reglist & (1 << i)) cnt++;
                if (r) cnt++;
                sp -= 4 * cnt;
                uint32_t addr = sp;
                for (int i = 0; i < 8; i++) {
                    if (reglist & (1 << i)) {
                        mem.write32(addr & ~3u, cpu.get_reg(i));
                        addr += 4;
                    }
                }
                if (r) mem.write32(addr & ~3u, cpu.r[14]);
                cpu.banks[3].r13 = sp;
            }'''
new = r'''            uint32_t sp = cpu.get_reg(13);
            if (l) {
                for (int i = 0; i < 8; i++) {
                    if (reglist & (1 << i)) {
                        cpu.set_reg(i, mem.read32(sp & ~3u));
                        sp += 4;
                    }
                }
                if (r) {
                    uint32_t pc = mem.read32(sp & ~3u);
                    sp += 4;
                    cpu.r[15] = pc & ~1u;
                }
                cpu.set_reg(13, sp);
            } else {
                int cnt = 0;
                for (int i = 0; i < 8; i++) if (reglist & (1 << i)) cnt++;
                if (r) cnt++;
                sp -= 4 * cnt;
                uint32_t addr = sp;
                for (int i = 0; i < 8; i++) {
                    if (reglist & (1 << i)) {
                        mem.write32(addr & ~3u, cpu.get_reg(i));
                        addr += 4;
                    }
                }
                if (r) mem.write32(addr & ~3u, cpu.r[14]);
                cpu.set_reg(13, sp);
            }'''
src = sub_once(src, old, new, "Thumb SP: PUSH/POP")

# ---------------------------------------------------------------
# 3. Thumb BL — раздельная обработка первой и второй половины
# ---------------------------------------------------------------
old = r'''        if ((op & 0xF0) == 0xF0) {
            int32_t off = (int32_t)(instr & 0x7FF);
            if (off & 0x400) off |= 0xFFFFF800;
            off <<= 1;
            cpu.r[14] = cpu.r[15] + 2 + off;
            return;
        }'''
new = r'''        if ((op & 0xF8) == 0xF0) {
            // BL, first half: LR = PC + SignExtend(imm11 << 12)
            uint32_t imm = instr & 0x7FF;
            if (imm & 0x400) imm |= 0xFFFFF800u;
            uint32_t PC = cpu.r[15] + 2;
            cpu.r[14] = PC + (imm << 12);
            return;
        }
        if ((op & 0xF8) == 0xF8) {
            // BL, second half: target = LR + PC + (imm11 << 1); LR = (I+4) | 1
            uint32_t imm = instr & 0x7FF;
            uint32_t PC = cpu.r[15] + 2;
            uint32_t LR = cpu.r[14];
            uint32_t target = LR + PC + (imm << 1);
            uint32_t ret = cpu.r[15] | 1;
            cpu.r[14] = ret;
            cpu.r[15] = target & ~1u;
            return;
        }'''
src = sub_once(src, old, new, "Thumb BL: first/second half")

# ---------------------------------------------------------------
# 4. Thumb undefined instruction (cond=0xE) -> exception
# ---------------------------------------------------------------
old = r'''            if (cond == 0xE) return;
            if (!cpu.cond(cond)) return;'''
new = r'''            if (cond == 0xE) {
                cpu.take_exception(0x04, Mode::Undefined, false);
                return;
            }
            if (!cpu.cond(cond)) return;'''
src = sub_once(src, old, new, "Thumb undefined")

# ---------------------------------------------------------------
# 5. MOV PC, Rm в ARM — убрать неявный BX (ARMv4T не имеет)
# ---------------------------------------------------------------
old = r'''            if (rd == 15) {
                if (opcode == 0xD && !s && !i) {
                    uint32_t target = op2;
                    if (target & 1) { cpu.cpsr.set_t(true);  cpu.r[15] = target & ~1u; }
                    else            { cpu.cpsr.set_t(false); cpu.r[15] = target & ~3u; }
                    return;
                }
                if (s) cpu.cpsr.raw = cpu.banks[cpu.bank_idx()].spsr;
                cpu.r[15] = result & ~3u;
            } else cpu.set_reg(rd, result);'''
new = r'''            if (rd == 15) {
                if (s) cpu.cpsr.raw = cpu.banks[cpu.bank_idx()].spsr;
                cpu.r[15] = result & ~3u;
            } else cpu.set_reg(rd, result);'''
src = sub_once(src, old, new, "MOV PC,Rm: не BX (ARMv4T)")

# ---------------------------------------------------------------
# 6. step(): prefetch abort отделяем от data abort
# ---------------------------------------------------------------
old = r'''        mem.fault = false;
        if (cpu.cpsr.t()) exec_thumb(fetch_thumb());
        else              exec_arm(fetch_arm());

        if (mem.fault) {
            std::printf("[ABORT] Data %s @ 0x%08X (PC=0x%08X)\n",
                        mem.fault_write ? "write" : "read",
                        mem.fault_addr, cpu.r[15]);
            cpu.take_data_abort();
            mem.fault = false;
        }'''
new = r'''        mem.fault = false;
        uint32_t _instr;
        if (cpu.cpsr.t()) {
            _instr = fetch_thumb();
            if (mem.fault) {
                std::printf("[ABORT] Prefetch (Thumb) @ 0x%08X (PC=0x%08X) IFSR=0x%X IFAR=0x%08X\n",
                            mem.fault_addr, cpu.r[15], cp15.ifsr, cp15.ifar);
                cpu.take_prefetch_abort();
                mem.fault = false;
                return;
            }
            exec_thumb((uint16_t)_instr);
        } else {
            _instr = fetch_arm();
            if (mem.fault) {
                std::printf("[ABORT] Prefetch (ARM) @ 0x%08X (PC=0x%08X) IFSR=0x%X IFAR=0x%08X\n",
                            mem.fault_addr, cpu.r[15], cp15.ifsr, cp15.ifar);
                cpu.take_prefetch_abort();
                mem.fault = false;
                return;
            }
            exec_arm(_instr);
        }

        if (mem.fault) {
            std::printf("[ABORT] Data %s @ 0x%08X (PC=0x%08X) DFSR=0x%X DFAR=0x%08X\n",
                        mem.fault_write ? "write" : "read",
                        mem.fault_addr, cpu.r[15], cp15.dfsr, cp15.dfar);
            cpu.take_data_abort();
            mem.fault = false;
        }'''
src = sub_once(src, old, new, "step(): prefetch vs data abort")

# ---------------------------------------------------------------
# 7. run_tests: VIC LDR смещение 28 -> 24 (данные на 0x20)
# ---------------------------------------------------------------
old = "0xE59F001C,  // LDR r0, [pc, #28]"
new = "0xE59F0018,  // LDR r0, [pc, #24]"
src = sub_once(src, old, new, "run_tests: VIC LDR offset")

# ---------------------------------------------------------------
# 8. SP804: разделить TimerMode(bit6) и OneShot(bit0)
# ---------------------------------------------------------------
old = r'''                t[0].control = v & 0xFF;
                t[0].enabled = (v & 0x80) != 0;
                t[0].oneshot = (v & 0x40) == 0;'''
new = r'''                t[0].control = v & 0xFF;
                t[0].enabled = (v & 0x80) != 0;
                { bool periodic = (v & 0x40) != 0;
                  bool oshot    = (v & 0x01) != 0;
                  t[0].oneshot  = oshot || !periodic; }'''
src = sub_once(src, old, new, "SP804 timer0: bit6/bit0")

old = r'''                t[1].control = v & 0xFF;
                t[1].enabled = (v & 0x80) != 0;
                t[1].oneshot = (v & 0x40) == 0;'''
new = r'''                t[1].control = v & 0xFF;
                t[1].enabled = (v & 0x80) != 0;
                { bool periodic = (v & 0x40) != 0;
                  bool oshot    = (v & 0x01) != 0;
                  t[1].oneshot  = oshot || !periodic; }'''
src = sub_once(src, old, new, "SP804 timer1: bit6/bit0")

p.write_text(src)
print("P0 РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО — проверь сообщения")
