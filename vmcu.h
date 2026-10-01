// vmcu.h — EP32cm: Virtual Micro Controller (co-processor offload) для ARM4
//   * 8 x 32-bit int регистров, 8 x float (IEEE-754, бит-в-бит)
//   * 64 КБ своей памяти + окно shared memory 4 КБ (в ARM4 = 0x70000)
//   * DMA-копирование блоков между ARM4 и EP32cm
//   * MMIO 0x10110000, IRQ source 16, tick 1:4
#ifndef ARM4_VMCU_H
#define ARM4_VMCU_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

class VMCU {
public:
    enum Op : uint8_t {
        NOP = 0x00, MOV = 0x01, ADD = 0x02, SUB = 0x03,
        AND = 0x04, OR  = 0x05, XOR = 0x06,
        SHL = 0x07, SHR = 0x08, MUL = 0x09, DIV = 0x0A,
        MOD = 0x0B, NEG = 0x0C, NOT = 0x0D, CMP = 0x0E,
        MOVI = 0x10, ADDI = 0x11, SUBI = 0x12,
        ANDI = 0x13, ORI  = 0x14, XORI = 0x15,
        SHLI = 0x16, SHRI = 0x17, MULI = 0x18, CMPI = 0x19,
        LDL = 0x20, LDB = 0x21, LDH = 0x22,
        STL = 0x23, STB = 0x24, STH = 0x25,
        JMP = 0x30, JZ = 0x31, JNZ = 0x32,
        JC = 0x33, JNC = 0x34,
        CALL = 0x35, RET = 0x36,
        JLT = 0x37, JGE = 0x38, JGT = 0x39, JLE = 0x3A,
        IN  = 0x40, OUT = 0x41, IRQ = 0x42, HLT = 0x43,
        SEI = 0x44, CLI = 0x45, RES = 0x46, YIELD = 0x47,
        FADD = 0x50, FSUB = 0x51, FMUL = 0x52, FDIV = 0x53,
        FSQRT = 0x54, FABS = 0x55, FNEG = 0x56, FMOV = 0x57,
        FCMP = 0x58, FCVTI = 0x59, FCVTF = 0x5A,
        FLDL = 0x5B, FSTL = 0x5C,
        FLDI = 0x5D, FLDZ = 0x5E, FLD1 = 0x5F,
        // Трансцендентные функции (нативный FPU)
        FSIN   = 0x60, FCOS   = 0x61, FTAN   = 0x62,
        FASIN  = 0x63, FACOS  = 0x64, FATAN  = 0x65,
        FATAN2 = 0x66,
        FEXP   = 0x67, FLOG   = 0x68, FPOW   = 0x69,
    };

    enum Reg : uint32_t {
        R_STATUS     = 0x00,
        R_CMD        = 0x04,
        R_ARG0       = 0x08,
        R_ARG1       = 0x0C,
        R_RES0       = 0x10,
        R_RES1       = 0x14,
        R_IRQ_STAT   = 0x18,
        R_IRQ_MASK   = 0x1C,
        R_IRQ_CLR    = 0x20,
        R_MEM_ADDR   = 0x30,
        R_MEM_DATA   = 0x34,
        R_PC         = 0x38,
        R_REGIDX     = 0x3C,
        R_REGVAL     = 0x40,
        R_FREGIDX    = 0x44,
        R_FREGVAL    = 0x48,
        // --- DMA ---
        R_DMA_SRC    = 0x50,   // адрес источника
        R_DMA_DST    = 0x54,   // адрес назначения
        R_DMA_LEN    = 0x58,   // длина в байтах
        R_DMA_DIR    = 0x5C,   // 0=ARM4→VMCU, 1=VMCU→ARM4
        R_DMA_GO     = 0x60,   // W 1 = старт
        R_DMA_STAT   = 0x64,   // 0=idle, 1=done, 2=error
        // --- Graphics (framebuffer ARM4) ---
        R_FB_BASE    = 0x68,   // адрес framebuffer в ARM4 памяти
        R_GFX_CMD    = 0x6C,   // 0=nop 1=pixel 2=fill 3=rect 4=clear 5=hline 6=vline
        R_GFX_X0     = 0x70,
        R_GFX_Y0     = 0x74,
        R_GFX_X1     = 0x78,
        R_GFX_Y1     = 0x7C,
        R_GFX_COLOR  = 0x80,   // RGB565
        R_GFX_CMD2   = 0x84,   // дополнительная команда
    };

    static constexpr uint32_t CMD_NOP     = 0;
    static constexpr uint32_t CMD_RESET   = 1;
    static constexpr uint32_t CMD_RUN     = 2;
    static constexpr uint32_t CMD_HALT    = 3;
    static constexpr uint32_t CMD_STEP    = 4;
    static constexpr uint32_t CMD_IRQ_ACK = 5;
    static constexpr uint32_t CMD_CALL    = 6;

    uint32_t    base;
    const char* name;

    static constexpr uint32_t MEM_SIZE     = 65536;
    static constexpr uint32_t TICK_RATE    = 4;
    static constexpr uint32_t STACK_TOP    = MEM_SIZE - 4;

    // Shared memory окно в VMCU: 0x8000..0x8FFF (4 КБ) = ARM4 0x70000..0x70FFF
    static constexpr uint32_t SHARED_BASE  = 0x8000;
    static constexpr uint32_t SHARED_SIZE  = 0x1000;

    uint8_t  mem[MEM_SIZE];
    uint32_t r[8]   = {0};
    float    fp[8]  = {0.0f};
    uint32_t pc     = 0;
    uint32_t sp     = STACK_TOP;
    uint32_t flags  = 0;

    bool running = false;
    bool halted  = false;
    bool irq_en  = false;
    uint32_t tick_div = 0;

    // MMIO
    uint32_t cmd      = 0;
    uint32_t arg0     = 0, arg1 = 0;
    uint32_t res0     = 0, res1 = 0;
    uint32_t irq_stat = 0;
    uint32_t irq_mask = 0;
    uint32_t mem_addr = 0;
    uint32_t reg_idx  = 0;
    uint32_t freg_idx = 0;

    // DMA
    uint32_t dma_src  = 0;
    uint32_t dma_dst  = 0;
    uint32_t dma_len  = 0;
    uint32_t dma_dir  = 0;
    uint32_t dma_stat = 0;

    // Graphics
    uint32_t fb_base   = 0;
    uint32_t gfx_x0    = 0, gfx_y0 = 0;
    uint32_t gfx_x1    = 0, gfx_y1 = 0;
    uint32_t gfx_color = 0;
    uint64_t gfx_ops   = 0;

    // Внешние указатели
    uint8_t* shared_ptr   = nullptr;    // окно shared (ARM4 0x70000)
    uint8_t* arm4_mem     = nullptr;    // вся память ARM4
    size_t   arm4_mem_size = 0;

    uint64_t cycles        = 0;
    uint64_t cmds_executed = 0;
    uint64_t dma_ops       = 0;
    uint64_t dma_bytes     = 0;

    VMCU(uint32_t b, const char* n) : base(b), name(n) {
        std::memset(mem, 0, sizeof(mem));
    }

    bool contains(uint32_t a) const { return a >= base && a < base + 0x1000; }

    void attach_shared(uint8_t* p) { shared_ptr = p; }
    void attach_arm4(uint8_t* p, size_t sz) { arm4_mem = p; arm4_mem_size = sz; }

    // ---------- Флаги ----------
    bool fz() const { return (flags >> 0) & 1; }
    bool fn() const { return (flags >> 1) & 1; }
    bool fc() const { return (flags >> 2) & 1; }
    bool fv() const { return (flags >> 3) & 1; }

    void set_flags_add(uint32_t a, uint32_t b, uint32_t s) {
        uint64_t full = (uint64_t)a + (uint64_t)b;
        flags = 0;
        if (s == 0) flags |= 1u << 0;
        if (s >> 31) flags |= 1u << 1;
        if (full > 0xFFFFFFFFu) flags |= 1u << 2;
        if (((a ^ s) & (b ^ s)) >> 31) flags |= 1u << 3;
    }
    void set_flags_sub(uint32_t a, uint32_t b, uint32_t s) {
        flags = 0;
        if (s == 0) flags |= 1u << 0;
        if (s >> 31) flags |= 1u << 1;
        if (a >= b) flags |= 1u << 2;
        if (((a ^ b) & (a ^ s)) >> 31) flags |= 1u << 3;
    }
    void fcmp(float a, float b) {
        flags = 0;
        if (std::isnan(a) || std::isnan(b)) { flags |= 1u << 3; return; }
        if (a == b) flags |= 1u << 0;
        if (a <  b) flags |= 1u << 1;
        if (a >  b) flags |= 1u << 2;
    }

    static float    bits_to_float(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
    static uint32_t float_to_bits(float f)    { uint32_t b; std::memcpy(&b, &f, 4); return b; }

    // ---------- Память ----------
    // shared helper: если a в окне shared и shared_ptr != nullptr, работаем с ним
    inline bool in_shared(uint32_t a, uint32_t n) const {
        return shared_ptr &&
               a >= SHARED_BASE && a + n <= SHARED_BASE + SHARED_SIZE;
    }
    inline bool overlaps_shared(uint32_t a, uint32_t n) const {
        return a < SHARED_BASE + SHARED_SIZE && a + n > SHARED_BASE;
    }

    uint32_t mem_read32(uint32_t a) {
        if (a + 4 > MEM_SIZE) { fault("MR32_OOB"); return 0; }
        if (in_shared(a, 4)) {
            uint32_t v; std::memcpy(&v, shared_ptr + (a - SHARED_BASE), 4); return v;
        }
        if (overlaps_shared(a, 4) && shared_ptr) { fault("SHARED_MISALIGN32"); return 0; }
        uint32_t v; std::memcpy(&v, &mem[a], 4); return v;
    }
    uint16_t mem_read16(uint32_t a) {
        if (a + 2 > MEM_SIZE) { fault("MR16_OOB"); return 0; }
        if (in_shared(a, 2)) {
            uint16_t v; std::memcpy(&v, shared_ptr + (a - SHARED_BASE), 2); return v;
        }
        if (overlaps_shared(a, 2) && shared_ptr) { fault("SHARED_MISALIGN16"); return 0; }
        uint16_t v; std::memcpy(&v, &mem[a], 2); return v;
    }
    uint8_t mem_read8(uint32_t a) {
        if (a >= MEM_SIZE) { fault("MR8_OOB"); return 0; }
        if (in_shared(a, 1)) return shared_ptr[a - SHARED_BASE];
        if (overlaps_shared(a, 1) && shared_ptr) { fault("SHARED_MISALIGN8"); return 0; }
        return mem[a];
    }
    void mem_write32(uint32_t a, uint32_t v) {
        if (a + 4 > MEM_SIZE) { fault("MW32_OOB"); return; }
        if (in_shared(a, 4)) {
            std::memcpy(shared_ptr + (a - SHARED_BASE), &v, 4); return;
        }
        if (overlaps_shared(a, 4) && shared_ptr) { fault("SHARED_MISALIGN32"); return; }
        std::memcpy(&mem[a], &v, 4);
    }
    void mem_write16(uint32_t a, uint16_t v) {
        if (a + 2 > MEM_SIZE) { fault("MW16_OOB"); return; }
        if (in_shared(a, 2)) {
            std::memcpy(shared_ptr + (a - SHARED_BASE), &v, 2); return;
        }
        if (overlaps_shared(a, 2) && shared_ptr) { fault("SHARED_MISALIGN16"); return; }
        std::memcpy(&mem[a], &v, 2);
    }
    void mem_write8(uint32_t a, uint8_t v) {
        if (a >= MEM_SIZE) { fault("MW8_OOB"); return; }
        if (in_shared(a, 1)) { shared_ptr[a - SHARED_BASE] = v; return; }
        if (overlaps_shared(a, 1) && shared_ptr) { fault("SHARED_MISALIGN8"); return; }
        mem[a] = v;
    }
    void push32(uint32_t v) { sp -= 4; mem_write32(sp, v); }
    uint32_t pop32() { uint32_t v = mem_read32(sp); sp += 4; return v; }

    void fault(const char* why) {
        std::printf("[VMCU] FAULT: %s at PC=0x%04X\n", why, pc);
        halted = true; running = false;
    }

    // ---------- Graphics ----------
    static constexpr int FB_W = 320;
    static constexpr int FB_H = 240;

    inline void fb_write(int x, int y, uint16_t c) {
        if (x < 0 || x >= FB_W || y < 0 || y >= FB_H) return;
        if (!arm4_mem) return;
        size_t off = fb_base + (size_t)y * FB_W * 2 + (size_t)x * 2;
        if (off + 2 > arm4_mem_size) return;
        std::memcpy(arm4_mem + off, &c, 2);
    }

    void gfx_exec(uint32_t cmd) {
        if (!arm4_mem) return;
        gfx_ops++;
        uint16_t color = (uint16_t)gfx_color;
        switch (cmd) {
            case 0: break;  // nop
            case 1: fb_write((int)gfx_x0, (int)gfx_y0, color); break;
            case 2:  // fill всё
                for (int y = 0; y < FB_H; y++)
                    for (int x = 0; x < FB_W; x++)
                        fb_write(x, y, color);
                break;
            case 3: {  // rect
                int x0 = (int)gfx_x0, y0 = (int)gfx_y0;
                int x1 = (int)gfx_x1, y1 = (int)gfx_y1;
                if (x0 > x1) std::swap(x0, x1);
                if (y0 > y1) std::swap(y0, y1);
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++)
                        fb_write(x, y, color);
                break;
            }
            case 4:  // clear (fill 0)
                for (int y = 0; y < FB_H; y++)
                    for (int x = 0; x < FB_W; x++)
                        fb_write(x, y, 0);
                break;
            case 5: {  // hline
                int x0 = (int)gfx_x0, x1 = (int)gfx_x1;
                if (x0 > x1) std::swap(x0, x1);
                for (int x = x0; x < x1; x++)
                    fb_write(x, (int)gfx_y0, color);
                break;
            }
            case 6: {  // vline
                int y0 = (int)gfx_y0, y1 = (int)gfx_y1;
                if (y0 > y1) std::swap(y0, y1);
                for (int y = y0; y < y1; y++)
                    fb_write((int)gfx_x0, y, color);
                break;
            }
            case 7: {  // line (Bresenham)
                int x0 = (int)gfx_x0, y0 = (int)gfx_y0;
                int x1 = (int)gfx_x1, y1 = (int)gfx_y1;
                int dx = std::abs(x1 - x0);
                int sx = x0 < x1 ? 1 : -1;
                int dy = -std::abs(y1 - y0);
                int sy = y0 < y1 ? 1 : -1;
                int err = dx + dy;
                while (1) {
                    fb_write(x0, y0, color);
                    if (x0 == x1 && y0 == y1) break;
                    int e2 = 2 * err;
                    if (e2 >= dy) { err += dy; x0 += sx; }
                    if (e2 <= dx) { err += dx; y0 += sy; }
                }
                break;
            }
            case 8: {  // circle (midpoint)
                int cx = (int)gfx_x0, cy = (int)gfx_y0;
                int r  = (int)gfx_x1;
                int x = r, y = 0, err = 1 - r;
                while (x >= y) {
                    fb_write(cx + x, cy + y, color);
                    fb_write(cx + y, cy + x, color);
                    fb_write(cx - y, cy + x, color);
                    fb_write(cx - x, cy + y, color);
                    fb_write(cx - x, cy - y, color);
                    fb_write(cx - y, cy - x, color);
                    fb_write(cx + y, cy - x, color);
                    fb_write(cx + x, cy - y, color);
                    y++;
                    if (err < 0) err += 2*y + 1;
                    else { x--; err += 2*(y - x) + 1; }
                }
                break;
            }
            default: break;
        }
    }

    // ---------- DMA ----------
    void do_dma() {
        dma_ops++;
        dma_stat = 0;
        if (!arm4_mem) { dma_stat = 2; return; }
        if (dma_len == 0 || dma_len > 0x10000) { dma_stat = 2; return; }
        if (dma_dir == 0) {
            // ARM4 -> VMCU
            if (dma_src + dma_len > arm4_mem_size) { dma_stat = 2; return; }
            if (dma_dst + dma_len > MEM_SIZE) { dma_stat = 2; return; }
            std::memcpy(mem + dma_dst, arm4_mem + dma_src, dma_len);
        } else {
            // VMCU -> ARM4
            if (dma_src + dma_len > MEM_SIZE) { dma_stat = 2; return; }
            if (dma_dst + dma_len > arm4_mem_size) { dma_stat = 2; return; }
            std::memcpy(arm4_mem + dma_dst, mem + dma_src, dma_len);
        }
        dma_stat = 1;
        dma_bytes += dma_len;
    }

    // ---------- Выполнение ----------
    bool step() {
        if (!running || halted) return false;
        if (pc + 4 > MEM_SIZE) { fault("PC_OOB"); return false; }

        uint32_t instr = mem_read32(pc);
        uint32_t op    = (instr >> 24) & 0xFF;
        uint32_t rd    = (instr >> 20) & 0xF;
        uint32_t rs    = (instr >> 16) & 0xF;
        uint32_t rt    = (instr >> 12) & 0xF;
        uint32_t imm20 = instr & 0xFFFFF;
        int32_t  simm20 = (int32_t)(imm20 << 12) >> 12;
        uint32_t port  = rs;
        uint32_t off16 = instr & 0xFFFF;
        int32_t  soff16 = (int32_t)(int16_t)off16;

        pc += 4;
        cycles++;
        #define RCK(x) if ((x) > 7) { fault("REG_OOB"); return false; }

        switch (op) {
            case NOP: break;
            case MOV: RCK(rd); RCK(rs); r[rd] = r[rs]; break;
            case ADD: RCK(rd); RCK(rs); RCK(rt);
                { uint32_t a = r[rs], b = r[rt], s = a + b;
                  r[rd] = s; set_flags_add(a, b, s); } break;
            case SUB: RCK(rd); RCK(rs); RCK(rt);
                { uint32_t a = r[rs], b = r[rt], s = a - b;
                  r[rd] = s; set_flags_sub(a, b, s); } break;
            case AND: RCK(rd); RCK(rs); RCK(rt); r[rd] = r[rs] & r[rt]; break;
            case OR:  RCK(rd); RCK(rs); RCK(rt); r[rd] = r[rs] | r[rt]; break;
            case XOR: RCK(rd); RCK(rs); RCK(rt); r[rd] = r[rs] ^ r[rt]; break;
            case SHL: RCK(rd); RCK(rs); RCK(rt); r[rd] = r[rs] << (r[rt] & 31); break;
            case SHR: RCK(rd); RCK(rs); RCK(rt); r[rd] = r[rs] >> (r[rt] & 31); break;
            case MUL: RCK(rd); RCK(rs); RCK(rt); r[rd] = r[rs] * r[rt]; break;
            case DIV: RCK(rd); RCK(rs); RCK(rt);
                if (r[rt] == 0) { fault("DIV0"); return false; }
                r[rd] = r[rs] / r[rt]; break;
            case MOD: RCK(rd); RCK(rs); RCK(rt);
                if (r[rt] == 0) { fault("MOD0"); return false; }
                r[rd] = r[rs] % r[rt]; break;
            case NEG: RCK(rd); RCK(rs);
                { uint32_t a = 0, b = r[rs], s = a - b;
                  r[rd] = s; set_flags_sub(a, b, s); } break;
            case NOT: RCK(rd); RCK(rs); r[rd] = ~r[rs]; break;
            case CMP: RCK(rs); RCK(rt);
                set_flags_sub(r[rs], r[rt], r[rs] - r[rt]); break;

            case MOVI: RCK(rd); r[rd] = imm20; break;
            case ADDI: RCK(rd);
                { uint32_t a = r[rd], s = a + imm20;
                  r[rd] = s; set_flags_add(a, imm20, s); } break;
            case SUBI: RCK(rd);
                { uint32_t a = r[rd], s = a - imm20;
                  r[rd] = s; set_flags_sub(a, imm20, s); } break;
            case ANDI: RCK(rd); r[rd] &= imm20; break;
            case ORI:  RCK(rd); r[rd] |= imm20; break;
            case XORI: RCK(rd); r[rd] ^= imm20; break;
            case SHLI: RCK(rd); r[rd] <<= (imm20 & 31); break;
            case SHRI: RCK(rd); r[rd] >>= (imm20 & 31); break;
            case MULI: RCK(rd); r[rd] *= imm20; break;
            case CMPI: RCK(rd);
                set_flags_sub(r[rd], imm20, r[rd] - imm20); break;

            case LDL: RCK(rd); RCK(rs); r[rd] = mem_read32(r[rs] + soff16); break;
            case LDB: RCK(rd); RCK(rs); r[rd] = mem_read8 (r[rs] + soff16); break;
            case LDH: RCK(rd); RCK(rs); r[rd] = mem_read16(r[rs] + soff16); break;
            case STL: RCK(rd); RCK(rs); mem_write32(r[rs] + soff16, r[rd]); break;
            case STB: RCK(rd); RCK(rs); mem_write8 (r[rs] + soff16, (uint8_t)r[rd]); break;
            case STH: RCK(rd); RCK(rs); mem_write16(r[rs] + soff16, (uint16_t)r[rd]); break;

            case JMP:  pc = pc + (uint32_t)(simm20 * 4); break;
            case JZ:   if (fz())  pc = pc + (uint32_t)(simm20 * 4); break;
            case JNZ:  if (!fz()) pc = pc + (uint32_t)(simm20 * 4); break;
            case JC:   if (fc())  pc = pc + (uint32_t)(simm20 * 4); break;
            case JNC:  if (!fc()) pc = pc + (uint32_t)(simm20 * 4); break;
            case JLT:  if (fn() != fv()) pc = pc + (uint32_t)(simm20 * 4); break;
            case JGE:  if (fn() == fv()) pc = pc + (uint32_t)(simm20 * 4); break;
            case JGT:  if (!fz() && (fn() == fv())) pc = pc + (uint32_t)(simm20 * 4); break;
            case JLE:  if (fz() || (fn() != fv()))  pc = pc + (uint32_t)(simm20 * 4); break;
            case CALL: push32(pc); pc = pc + (uint32_t)(simm20 * 4); break;
            case RET:  pc = pop32(); break;

            case IN:  RCK(rd); r[rd] = io_read(port); break;
            case OUT: RCK(rd); io_write(port, r[rd]); break;
            case IRQ: if (irq_en) irq_stat |= (1u << (port & 0x1F)); break;
            case HLT: halted = true; running = false; break;
            case SEI: irq_en = true; break;
            case CLI: irq_en = false; break;
            case RES: do_reset(); break;
            case YIELD: break;

            case FADD: RCK(rd); RCK(rs); RCK(rt); fp[rd] = fp[rs] + fp[rt]; break;
            case FSUB: RCK(rd); RCK(rs); RCK(rt); fp[rd] = fp[rs] - fp[rt]; break;
            case FMUL: RCK(rd); RCK(rs); RCK(rt); fp[rd] = fp[rs] * fp[rt]; break;
            case FDIV: RCK(rd); RCK(rs); RCK(rt); fp[rd] = fp[rs] / fp[rt]; break;
            case FSQRT: RCK(rd); RCK(rs);
                fp[rd] = (fp[rs] < 0.0f) ? std::nanf("") : std::sqrt(fp[rs]); break;
            case FABS: RCK(rd); RCK(rs); fp[rd] = std::fabs(fp[rs]); break;
            case FNEG: RCK(rd); RCK(rs); fp[rd] = -fp[rs]; break;
            case FMOV: RCK(rd); RCK(rs); fp[rd] = fp[rs]; break;
            case FCMP: RCK(rs); RCK(rt); fcmp(fp[rs], fp[rt]); break;
            case FCVTI: RCK(rd); RCK(rs); fp[rd] = (float)(int32_t)r[rs]; break;
            case FCVTF: RCK(rd); RCK(rs); r[rd] = (uint32_t)(int32_t)fp[rs]; break;
            case FLDL: RCK(rd); RCK(rs);
                { uint32_t b = mem_read32(r[rs] + soff16); fp[rd] = bits_to_float(b); } break;
            case FSTL: RCK(rd); RCK(rs);
                mem_write32(r[rs] + soff16, float_to_bits(fp[rd])); break;
            case FLDI: RCK(rd); fp[rd] = (float)(int32_t)imm20; break;
            case FLDZ: RCK(rd); fp[rd] = 0.0f; break;
            case FLD1: RCK(rd); fp[rd] = 1.0f; break;

            // --- Трансцендентные функции (унарные: [op][fd][fs]) ---
            case FSIN:   RCK(rd); RCK(rs); fp[rd] = std::sin(fp[rs]); break;
            case FCOS:   RCK(rd); RCK(rs); fp[rd] = std::cos(fp[rs]); break;
            case FTAN:   RCK(rd); RCK(rs); fp[rd] = std::tan(fp[rs]); break;
            case FASIN:  RCK(rd); RCK(rs); fp[rd] = std::asin(fp[rs]); break;
            case FACOS:  RCK(rd); RCK(rs); fp[rd] = std::acos(fp[rs]); break;
            case FATAN:  RCK(rd); RCK(rs); fp[rd] = std::atan(fp[rs]); break;
            case FEXP:   RCK(rd); RCK(rs); fp[rd] = std::exp(fp[rs]); break;
            case FLOG:   RCK(rd); RCK(rs); fp[rd] = std::log(fp[rs]); break;
            // --- Бинарные ---
            case FATAN2: RCK(rd); RCK(rs); RCK(rt);
                fp[rd] = std::atan2(fp[rs], fp[rt]); break;
            case FPOW:   RCK(rd); RCK(rs); RCK(rt);
                fp[rd] = std::pow(fp[rs], fp[rt]); break;

            default:
                std::printf("[VMCU] Unknown op 0x%02X at PC=0x%04X\n", op, pc - 4);
                fault("UNKNOWN_OP");
                return false;
        }
        #undef RCK
        return running && !halted;
    }

    void tick() {
        if (!running || halted) return;
        if (++tick_div >= TICK_RATE) { tick_div = 0; step(); }
    }

    void do_reset() {
        std::memset(r, 0, sizeof(r));
        std::memset(fp, 0, sizeof(fp));
        pc = 0; sp = STACK_TOP; flags = 0;
        halted = false; running = false; irq_en = false;
        tick_div = 0; irq_stat = 0;
        cmd = 0; arg0 = arg1 = 0; res0 = res1 = 0;
        mem_addr = 0; reg_idx = 0; freg_idx = 0;
        dma_src = dma_dst = dma_len = dma_dir = 0; dma_stat = 0;
    }

    uint32_t io_read(uint32_t port) {
        switch (port) {
            case 0: return (uint32_t)cycles;
            case 1: return 0x564D4355u;
            case 2: return arg0;
            case 3: return arg1;
            default: return 0;
        }
    }
    void io_write(uint32_t port, uint32_t v) {
        switch (port) {
            case 0: res0 = v; break;
            case 1: res1 = v; break;
            case 2: irq_stat |= 1u; break;
            // GFX-порты (для use из VMCU-программ)
            case 5:  fb_base   = v; break;
            case 6:  gfx_exec(v); break;
            case 7:  gfx_x0    = v; break;
            case 8:  gfx_y0    = v; break;
            case 9:  gfx_x1    = v; break;
            case 10: gfx_y1    = v; break;
            case 11: gfx_color = v; break;
            case 12: gfx_exec(v); break;   // alias для GFX_CMD (для удобства)
            default: break;
        }
    }

    bool irq_pending() const { return irq_stat != 0; }
    uint32_t status() const {
        uint32_t s = 0;
        if (running) s |= 0x01;
        if (halted)  s |= 0x02;
        if (irq_pending()) s |= 0x04;
        return s;
    }

    uint32_t read(uint32_t addr, int width) {
        uint32_t off = addr - base;
        uint32_t v = 0;
        switch (off & ~3u) {
            case R_STATUS:   v = status(); break;
            case R_CMD:      v = cmd; break;
            case R_ARG0:     v = arg0; break;
            case R_ARG1:     v = arg1; break;
            case R_RES0:     v = res0; break;
            case R_RES1:     v = res1; break;
            case R_IRQ_STAT: v = irq_stat; break;
            case R_IRQ_MASK: v = irq_mask; break;
            case R_MEM_ADDR: v = mem_addr; break;
            case R_MEM_DATA: v = (mem_addr < MEM_SIZE) ? mem[mem_addr] : 0; break;
            case R_PC:       v = pc; break;
            case R_REGIDX:   v = reg_idx; break;
            case R_REGVAL:   v = (reg_idx < 8) ? r[reg_idx] : 0; break;
            case R_FREGIDX:  v = freg_idx; break;
            case R_FREGVAL:  v = (freg_idx < 8) ? float_to_bits(fp[freg_idx]) : 0; break;
            case R_DMA_SRC:  v = dma_src; break;
            case R_DMA_DST:  v = dma_dst; break;
            case R_DMA_LEN:  v = dma_len; break;
            case R_DMA_DIR:  v = dma_dir; break;
            case R_DMA_GO:   v = 0; break;  // write-only
            case R_DMA_STAT: v = dma_stat; break;
            case R_FB_BASE:   v = fb_base; break;
            case R_GFX_CMD:   v = 0; break;
            case R_GFX_X0:    v = gfx_x0; break;
            case R_GFX_Y0:    v = gfx_y0; break;
            case R_GFX_X1:    v = gfx_x1; break;
            case R_GFX_Y1:    v = gfx_y1; break;
            case R_GFX_COLOR: v = gfx_color; break;
            default: v = 0;
        }
        if (width == 2) v = (v >> ((addr & 2) * 8)) & 0xFFFF;
        else if (width == 1) v = (v >> ((addr & 3) * 8)) & 0xFF;
        return v;
    }

    void write(uint32_t addr, uint32_t v, int width) {
        (void)width;
        uint32_t off = addr - base;
        switch (off & ~3u) {
            case R_CMD:      cmd = v; handle_cmd(v); break;
            case R_ARG0:     arg0 = v; break;
            case R_ARG1:     arg1 = v; break;
            case R_RES0:     res0 = v; break;
            case R_RES1:     res1 = v; break;
            case R_IRQ_MASK: irq_mask = v; break;
            case R_IRQ_CLR:  irq_stat &= ~v; break;
            case R_MEM_ADDR: mem_addr = v & 0xFFFF; break;
            case R_MEM_DATA:
                if (mem_addr < MEM_SIZE) {
                    mem[mem_addr] = (uint8_t)v;
                    mem_addr = (mem_addr + 1) & 0xFFFF;
                }
                break;
            case R_PC:       pc = v & 0xFFFF; break;
            case R_REGIDX:   reg_idx = v & 0xF; break;
            case R_REGVAL:   if (reg_idx < 8) r[reg_idx] = v; break;
            case R_FREGIDX:  freg_idx = v & 0xF; break;
            case R_FREGVAL:  if (freg_idx < 8) fp[freg_idx] = bits_to_float(v); break;
            case R_DMA_SRC:  dma_src = v; break;
            case R_DMA_DST:  dma_dst = v; break;
            case R_DMA_LEN:  dma_len = v & 0xFFFF; break;
            case R_DMA_DIR:  dma_dir = v & 1; break;
            case R_DMA_GO:   if (v & 1) do_dma(); break;
            case R_DMA_STAT: dma_stat = v; break;
            case R_FB_BASE:   fb_base = v; break;
            case R_GFX_CMD:   gfx_exec(v); break;
            case R_GFX_X0:    gfx_x0 = v; break;
            case R_GFX_Y0:    gfx_y0 = v; break;
            case R_GFX_X1:    gfx_x1 = v; break;
            case R_GFX_Y1:    gfx_y1 = v; break;
            case R_GFX_COLOR: gfx_color = v; break;
        }
    }

    void handle_cmd(uint32_t c) {
        cmds_executed++;
        switch (c) {
            case CMD_NOP: break;
            case CMD_RESET: do_reset(); break;
            case CMD_RUN:   running = true; halted = false; break;
            case CMD_HALT:  running = false; break;
            case CMD_STEP:  step(); break;
            case CMD_IRQ_ACK: irq_stat = 0; break;
            case CMD_CALL:  pc = arg0 & 0xFFFF; running = true; halted = false; break;
        }
    }

    void dump() const {
        std::printf("--- EP32cm @ 0x%08X (%s) ---\n", base, name);
        std::printf("  state: %s%s%s  cycles=%llu cmds=%llu\n",
                    running ? "RUN" : "---", halted ? " HLT" : "",
                    irq_en ? " IRQ-EN" : "",
                    (unsigned long long)cycles,
                    (unsigned long long)cmds_executed);
        std::printf("  PC=0x%04X SP=0x%04X FLAGS=0x%X (Z=%d N=%d C=%d V=%d)\n",
                    pc, sp, flags, (int)fz(), (int)fn(), (int)fc(), (int)fv());
        std::printf("  R: ");
        for (int i = 0; i < 8; i++) std::printf("R%d=0x%08X ", i, r[i]);
        std::printf("\n  F: ");
        for (int i = 0; i < 8; i++) std::printf("F%d=%.6g ", i, (double)fp[i]);
        std::printf("\n");
        std::printf("  MMIO: cmd=0x%X arg0=0x%X arg1=0x%X res0=0x%X res1=0x%X\n",
                    cmd, arg0, arg1, res0, res1);
        std::printf("  IRQ: stat=0x%X mask=0x%X\n", irq_stat, irq_mask);
        std::printf("  GFX: fb=0x%08X color=0x%04X ops=%llu\n",
                    fb_base, gfx_color, (unsigned long long)gfx_ops);
        std::printf("  DMA: src=0x%X dst=0x%X len=%u dir=%u stat=%u ops=%llu bytes=%llu\n",
                    dma_src, dma_dst, dma_len, dma_dir, dma_stat,
                    (unsigned long long)dma_ops,
                    (unsigned long long)dma_bytes);
        std::printf("  SHARED: %s\n", shared_ptr ? "attached" : "null");
    }
};

#endif // ARM4_VMCU_H
