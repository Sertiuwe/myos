// mmu.h — CP15 + полноценный MMU для ARMv4 (v18.1)
// Поддержка: L1-секции 1МБ, L2 coarse (страницы 4КБ),
// AP-биты (RO/RW, user/supervisor), домены, permission/translation faults.
#ifndef ARM4_MMU_H
#define ARM4_MMU_H

#include <cstdint>
#include <cstdio>
#include <cstring>

// Типы ошибок (DFSR/IFSR биты [3:0])
#define MMU_FSR_ALIGN    0x1
#define MMU_FSR_TRANS    0x5
#define MMU_FSR_DOMAIN   0x9
#define MMU_FSR_PERM     0xD

class CP15;
class MMU;
extern CP15* g_cp15_ptr;
extern MMU*  g_mmu_ptr;

class CP15 {
public:
    uint32_t sctlr = 0;
    uint32_t ttbr0 = 0;
    uint32_t ttbr1 = 0;
    uint32_t ttbcr = 0;
    uint32_t dacr  = 0x00000003;  // domain 0 = client, domain 1 = client
    uint32_t dfsr  = 0;
    uint32_t ifsr  = 0;
    uint32_t dfar  = 0;
    uint32_t ifar  = 0;
    uint32_t cpuid = 0x4100F000;   // ARM7TDMI-ish
    uint32_t cache_id = 0x00000000;
    bool     tlb_dirty = false;

    bool mmu_on()    const { return (sctlr & 0x1) != 0; }
    bool align_on()  const { return (sctlr & 0x2) != 0; }
    bool dcache_on() const { return (sctlr & 0x4) != 0; }
    bool wbuf_on()   const { return (sctlr & 0x8) != 0; }
    bool icache_on() const { return (sctlr & 0x1000) != 0; }

    int domain_access(int d) const {
        return (dacr >> (d * 2)) & 0x3;
    }

    uint32_t read(uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2) {
        (void)op1; (void)op2;
        switch (crn) {
            case 0: return cpuid;
            case 1: return sctlr;
            case 2: return (crm == 0) ? ttbr0 : (crm == 1 ? ttbr1 : ttbcr);
            case 3: return dacr;
            case 5: return (crm == 0) ? dfsr : ifsr;
            case 6: return (crm == 0) ? dfar : ifar;
            case 7: return 0;      // cache maintenance
            case 8: return 0;      // TLB ops
            default: return 0;
        }
    }
    void write(uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2, uint32_t v) {
        (void)op1; (void)op2;
        switch (crn) {
            case 1: sctlr = v; break;
            case 2:
                if (crm == 0) ttbr0 = v & 0xFFFFC000u;
                else if (crm == 1) ttbr1 = v & 0xFFFFC000u;
                else if (crm == 2) ttbcr = v & 0x7;
                break;
            case 3: dacr = v; break;
            case 5: if (crm == 0) dfsr = v; else ifsr = v; break;
            case 6: if (crm == 0) dfar = v; else ifar = v; break;
            case 7:
            case 8:
                tlb_dirty = true;
                break;
            default: break;
        }
    }
    void dump() const {
        std::printf("--- CP15 ---\n");
        std::printf("  SCTLR=0x%08X  M=%d A=%d C=%d W=%d I=%d\n",
            sctlr,(sctlr>>0)&1,(sctlr>>1)&1,(sctlr>>2)&1,(sctlr>>3)&1,(sctlr>>12)&1);
        std::printf("  TTBR0=0x%08X TTBR1=0x%08X TTBCR=%u\n", ttbr0, ttbr1, ttbcr);
        std::printf("  DACR=0x%08X  DFSR=0x%X  IFSR=0x%X  DFAR=0x%08X  IFAR=0x%08X\n",
                    dacr, dfsr, ifsr, dfar, ifar);
        std::printf("  Domain: ");
        for (int i = 0; i < 16; i++) {
            int d = domain_access(i);
            const char* s = (d==0)?"no":(d==1)?"cl":(d==2)?"rs":(d==3)?"mg":"??";
            std::printf("%s ", s);
        }
        std::printf("\n");
    }
};

class MMU {
public:
    static constexpr int TLB_SIZE = 1024;
    struct TLBEntry {
        uint32_t vaddr_base = 0;
        uint32_t paddr_base = 0;
        uint32_t size       = 0;
        bool     valid      = false;
    };
    TLBEntry tlb[TLB_SIZE];
    int next_slot = 0;
    uint64_t translations = 0, faults = 0;
    uint64_t perm_faults  = 0, trans_faults = 0;

    uint8_t* mem = nullptr;
    size_t   mem_size = 0;

    void attach(uint8_t* m, size_t n) { mem = m; mem_size = n; }
    void flush() { for (auto& e : tlb) e.valid = false; }

    uint32_t fetch_l1(uint32_t addr) {
        if (!mem || addr + 4 > mem_size) return 0;
        uint32_t v; std::memcpy(&v, mem + addr, 4); return v;
    }

    void tlb_add(uint32_t va, uint32_t pa, uint32_t sz) {
        int s = next_slot = (next_slot + 1) % TLB_SIZE;
        tlb[s] = { va & ~(sz - 1), pa & ~(sz - 1), sz, true };
    }

    // Возвращает физический адрес или va при fault
    uint32_t translate(uint32_t va, bool write, bool user, bool fetch,
                       bool& fault, int& fsr_code, CP15& cp15)
    {
        fault = false;
        fsr_code = 0;
        if (cp15.tlb_dirty) { flush(); cp15.tlb_dirty = false; }
        translations++;

        // TLB lookup
        for (auto& e : tlb) {
            if (e.valid && va >= e.vaddr_base && va < e.vaddr_base + e.size)
                return e.paddr_base + (va - e.vaddr_base);
        }

        uint32_t ttbr = cp15.ttbr0 & 0xFFFFC000u;
        uint32_t l1_idx = (va >> 20) & 0xFFF;
        uint32_t l1 = fetch_l1(ttbr + l1_idx * 4);
        uint32_t type = l1 & 0x3;

        // Section (1 МБ)
        if (type == 0x2) {
            int dom = (l1 >> 5) & 0xF;
            int acc = cp15.domain_access(dom);
            if (acc == 0) {          // no access
                fault = true; fsr_code = MMU_FSR_DOMAIN; goto fail;
            }
            if (acc == 1) {          // client — проверяем AP
                int ap = (l1 >> 10) & 0x3;
                bool ap_ro  = (ap & 1) != 0;
                bool ap_usr = (ap & 2) != 0;
                if (ap_ro && write) { fault = true; fsr_code = MMU_FSR_PERM; perm_faults++; goto fail; }
                if (!ap_usr && user) { fault = true; fsr_code = MMU_FSR_PERM; perm_faults++; goto fail; }
            }
            uint32_t pa = (l1 & 0xFFF00000u) | (va & 0xFFFFFu);
            tlb_add(va, pa, 0x100000);
            return pa;
        }

        // Coarse L2-таблица (4 КБ)
        if (type == 0x1) {
            uint32_t l2base = l1 & 0xFFFFFC00u;
            uint32_t l2i = (va >> 12) & 0xFF;
            uint32_t l2 = fetch_l1(l2base + l2i * 4);
            uint32_t l2t = l2 & 0x3;
            if (l2t == 0x2) {        // small page 4 КБ
                int dom = (l1 >> 5) & 0xF;
                int acc = cp15.domain_access(dom);
                if (acc == 0) { fault = true; fsr_code = MMU_FSR_DOMAIN; goto fail; }
                if (acc == 1) {
                    int ap2 = (l2 >> 4) & 0x3;   // AP[2:0] упрощённо: [5:4]
                    int ap10 = l2 & 0x3;          // AP[1:0] — на самом деле [5:4] и [9:8]
                    // Упростим: используем биты [5:4] и [9:8]
                    int ap = ((l2 >> 4) & 0x3) | (((l2 >> 6) & 0x3) << 2);
                    // AP[2]=RO для супервизора, AP[1]=user, AP[0]=RW
                    bool ap_ro  = (ap & 0x4) != 0;   // AP[2]
                    bool ap_usr = (ap & 0x2) != 0;   // AP[1]
                    (void)ap10; (void)ap2;
                    if (ap_ro && write) { fault = true; fsr_code = MMU_FSR_PERM; perm_faults++; goto fail; }
                    if (!ap_usr && user) { fault = true; fsr_code = MMU_FSR_PERM; perm_faults++; goto fail; }
                }
                uint32_t pa = (l2 & 0xFFFFF000u) | (va & 0xFFFu);
                tlb_add(va, pa, 0x1000);
                return pa;
            }
        }

        // Fault
    fail:
        if (fsr_code == 0) fsr_code = MMU_FSR_TRANS;   // invalid descriptor
        faults++; trans_faults++;
        if (fetch) {
            cp15.ifar = va;
            cp15.ifsr = fsr_code;
        } else {
            cp15.dfar = va;
            cp15.dfsr = fsr_code;
        }
        return va;
    }

    void dump() const {
        int used = 0;
        for (auto& e : tlb) if (e.valid) used++;
        std::printf("--- MMU ---\n");
        std::printf("  translations=%llu  faults=%llu (trans=%llu perm=%llu)  TLB=%d/%d\n",
            (unsigned long long)translations,
            (unsigned long long)faults,
            (unsigned long long)trans_faults,
            (unsigned long long)perm_faults,
            used, TLB_SIZE);
    }
    void dump_tlb() const {
        std::printf("--- TLB ---\n");
        int shown = 0;
        for (int i = 0; i < TLB_SIZE && shown < 32; i++)
            if (tlb[i].valid) {
                std::printf("  [%3d] v=0x%08X p=0x%08X size=0x%X\n",
                    i, tlb[i].vaddr_base, tlb[i].paddr_base, tlb[i].size);
                shown++;
            }
        if (shown == 0) std::printf("  (empty)\n");
    }
};

#endif
