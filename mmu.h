// mmu.h — CP15 + MMU для ARMv4T (v19.1)
//   * L1: sections 1MB, coarse L2 (small pages 4KB)
//   * AP-биты (RO/RW, priv/user), 16 доменов (no/client/manager)
//   * Секционные и страничные fault-коды в DFSR/IFSR
//   * TLB с кешированием прав
//   * CP15 c7/c8 реально инвалидируют I/D cache через Cache*
//   * Без goto — компилируется clang++ -std=c++17
#ifndef ARM4_MMU_H
#define ARM4_MMU_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "cache.h"

// ---------- FSR коды (ARMv4) ----------
#define MMU_FSR_ALIGN            0x1   // Alignment
#define MMU_FSR_TRANS_SECTION    0x5   // Section translation
#define MMU_FSR_TRANS_PAGE       0x7   // Page translation
#define MMU_FSR_DOMAIN_SECTION   0x9   // Section domain
#define MMU_FSR_DOMAIN_PAGE      0xB   // Page domain
#define MMU_FSR_PERM_SECTION     0xD   // Section permission
#define MMU_FSR_PERM_PAGE        0xF   // Page permission

class CP15;
class MMU;

class CP15 {
public:
    uint32_t sctlr    = 0;
    uint32_t ttbr0    = 0;
    uint32_t ttbr1    = 0;
    uint32_t ttbcr    = 0;
    uint32_t dacr     = 0xFFFFFFFF;
    uint32_t dfsr     = 0;
    uint32_t ifsr     = 0;
    uint32_t dfar     = 0;
    uint32_t ifar     = 0;
    uint32_t cpuid    = 0x4100F000;
    uint32_t cache_id = 0;

    Cache* icache_ptr = nullptr;
    Cache* dcache_ptr = nullptr;
    bool tlb_dirty = false;

    bool mmu_on()    const { return (sctlr & 0x1)    != 0; }
    bool align_on()  const { return (sctlr & 0x2)    != 0; }
    bool dcache_on() const { return (sctlr & 0x4)    != 0; }
    bool wbuf_on()   const { return (sctlr & 0x8)    != 0; }
    bool rom_prot()  const { return (sctlr & 0x200)  != 0; }
    bool icache_on() const { return (sctlr & 0x1000) != 0; }

    int domain_access(int d) const { return (dacr >> (d * 2)) & 0x3; }

    uint32_t read(uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2) {
        (void)op1; (void)op2;
        switch (crn) {
            case 0: return cpuid;
            case 1: return sctlr;
            case 2:
                if (crm == 0) return ttbr0;
                if (crm == 1) return ttbr1;
                if (crm == 2) return ttbcr;
                return 0;
            case 3: return dacr;
            case 5: return (crm == 0) ? dfsr : ifsr;
            case 6: return (crm == 0) ? dfar : ifar;
            case 7: return 0;
            case 8: return 0;
            case 9: return cache_id;
            default: return 0;
        }
    }

    void write(uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2, uint32_t v) {
        (void)op1; (void)op2;
        switch (crn) {
            case 1:
                sctlr = v;
                break;
            case 2:
                if (crm == 0) ttbr0 = v & 0xFFFFC000u;
                else if (crm == 1) ttbr1 = v & 0xFFFFC000u;
                else if (crm == 2) ttbcr = v & 0x7;
                break;
            case 3:
                dacr = v;
                break;
            case 5:
                if (crm == 0) dfsr = v; else ifsr = v;
                break;
            case 6:
                if (crm == 0) dfar = v; else ifar = v;
                break;
            case 7:
                if (crm == 5)      { if (icache_ptr) icache_ptr->invalidate_all(); }
                else if (crm == 6) { if (dcache_ptr) dcache_ptr->invalidate_all(); }
                else if (crm == 7) { if (icache_ptr) icache_ptr->invalidate_all();
                                     if (dcache_ptr) dcache_ptr->invalidate_all(); }
                else if (crm == 8) { if (icache_ptr) icache_ptr->invalidate_all(); }
                else if (crm == 10){ /* clean D — no-op для write-through */ }
                break;
            case 8:
                tlb_dirty = true;
                break;
            default:
                break;
        }
    }

    void dump() const {
        std::printf("--- CP15 ---\n");
        std::printf("  SCTLR=0x%08X  M=%d A=%d C=%d W=%d R=%d I=%d\n",
            sctlr,(sctlr>>0)&1,(sctlr>>1)&1,(sctlr>>2)&1,(sctlr>>3)&1,
            (sctlr>>9)&1,(sctlr>>12)&1);
        std::printf("  TTBR0=0x%08X  TTBR1=0x%08X  TTBCR=%u\n", ttbr0, ttbr1, ttbcr);
        std::printf("  DACR=0x%08X\n", dacr);
        std::printf("  Domain: ");
        for (int i = 0; i < 16; i++) {
            int d = domain_access(i);
            const char* s = (d==0)?"no":(d==1)?"cl":(d==2)?"rs":(d==3)?"mg":"??";
            std::printf("%s ", s);
        }
        std::printf("\n");
        std::printf("  DFSR=0x%X  IFSR=0x%X  DFAR=0x%08X  IFAR=0x%08X\n",
                    dfsr, ifsr, dfar, ifar);
        std::printf("  ICache=%s  DCache=%s\n",
                    icache_ptr ? "attached" : "null",
                    dcache_ptr ? "attached" : "null");
    }
};

class MMU {
public:
    static constexpr int TLB_SIZE = 512;
    struct TLBEntry {
        uint32_t vaddr_base = 0;
        uint32_t paddr_base = 0;
        uint32_t size       = 0;
        uint8_t  domain     = 0;
        uint8_t  ap         = 0;
        bool     is_section = false;
        bool     valid      = false;
    };
    TLBEntry tlb[TLB_SIZE];
    int next_slot = 0;

    uint64_t translations  = 0;
    uint64_t faults        = 0;
    uint64_t trans_faults  = 0;
    uint64_t perm_faults   = 0;
    uint64_t domain_faults = 0;
    uint64_t tlb_hits      = 0;
    uint64_t tlb_misses    = 0;

    uint8_t* mem = nullptr;
    size_t   mem_size = 0;

    void attach(uint8_t* m, size_t n) { mem = m; mem_size = n; }
    void flush() { for (auto& e : tlb) e.valid = false; }

    uint32_t fetch_pa32(uint32_t pa) const {
        if (!mem || pa + 4 > mem_size) return 0;
        uint32_t v; std::memcpy(&v, mem + pa, 4); return v;
    }

    TLBEntry* tlb_find(uint32_t va) {
        for (auto& e : tlb) {
            if (e.valid && va >= e.vaddr_base && va < e.vaddr_base + e.size)
                return &e;
        }
        return nullptr;
    }
    void tlb_add(uint32_t va, uint32_t pa, uint32_t sz, uint8_t dom,
                 uint8_t ap, bool section) {
        next_slot = (next_slot + 1) % TLB_SIZE;
        TLBEntry& e = tlb[next_slot];
        e.vaddr_base = va & ~(sz - 1);
        e.paddr_base = pa & ~(sz - 1);
        e.size       = sz;
        e.domain     = dom;
        e.ap         = ap;
        e.is_section = section;
        e.valid      = true;
    }

    // true = доступ разрешён
    static bool check_perm(uint8_t ap, bool is_section, bool user, bool write) {
        if (is_section) {
            switch (ap & 0x3) {
                case 0: return false;
                case 1: return !user;
                case 2: return !(user && write);
                case 3: return true;
            }
            return false;
        } else {
            if ((ap & 0xF) == 0x0) return false;
            if (ap & 0x8) return !write;
            if (ap & 0x4) return !write;
            switch (ap & 0x3) {
                case 0: return false;
                case 1: return !user;
                case 2: return !(user && write);
                case 3: return true;
            }
            return false;
        }
    }

    // Общая обработка fault-ситуации. Возвращает va.
    uint32_t do_fail(uint32_t va, bool fetch, bool write, CP15& cp15, int fsr_code) {
        faults++;
        if (fsr_code == MMU_FSR_TRANS_SECTION || fsr_code == MMU_FSR_TRANS_PAGE)
            trans_faults++;
        if (fetch) {
            cp15.ifar = va;
            cp15.ifsr = (uint32_t)(fsr_code & 0xF);
        } else {
            cp15.dfar = va;
            cp15.dfsr = (uint32_t)(fsr_code & 0xF) | (write ? (1u << 11) : 0);
        }
        return va;
    }

    uint32_t translate(uint32_t va, bool write, bool user, bool fetch,
                       bool& fault, int& fsr_code, CP15& cp15)
    {
        fault = false;
        fsr_code = 0;
        translations++;

        if (cp15.tlb_dirty) { flush(); cp15.tlb_dirty = false; }

        // ---- TLB lookup ----
        if (TLBEntry* e = tlb_find(va)) {
            tlb_hits++;
            int acc = cp15.domain_access(e->domain);
            if (acc == 0) {
                fault = true; fsr_code = MMU_FSR_DOMAIN_SECTION;
                domain_faults++;
                return do_fail(va, fetch, write, cp15, fsr_code);
            }
            if (acc == 1) {
                if (!check_perm(e->ap, e->is_section, user, write)) {
                    fault = true;
                    fsr_code = e->is_section ? MMU_FSR_PERM_SECTION
                                             : MMU_FSR_PERM_PAGE;
                    perm_faults++;
                    return do_fail(va, fetch, write, cp15, fsr_code);
                }
            }
            return e->paddr_base + (va - e->vaddr_base);
        }
        tlb_misses++;

        // ---- L1 ----
        uint32_t ttbr   = cp15.ttbr0 & 0xFFFFC000u;
        uint32_t l1_idx = (va >> 20) & 0xFFF;
        uint32_t l1     = fetch_pa32(ttbr + l1_idx * 4);
        uint32_t type   = l1 & 0x3;

        // Section (1 MB)
        if (type == 0x2) {
            uint8_t dom = (uint8_t)((l1 >> 5) & 0xF);
            uint8_t ap  = (uint8_t)((l1 >> 10) & 0x3);
            int acc = cp15.domain_access(dom);
            if (acc == 0) {
                fault = true; fsr_code = MMU_FSR_DOMAIN_SECTION;
                domain_faults++;
                return do_fail(va, fetch, write, cp15, fsr_code);
            }
            if (acc == 1) {
                if (!check_perm(ap, true, user, write)) {
                    fault = true; fsr_code = MMU_FSR_PERM_SECTION;
                    perm_faults++;
                    return do_fail(va, fetch, write, cp15, fsr_code);
                }
            }
            uint32_t pa = (l1 & 0xFFF00000u) | (va & 0xFFFFFu);
            tlb_add(va, pa, 0x100000, dom, ap, true);
            return pa;
        }

        // Coarse L2 (4 KB small pages)
        if (type == 0x1) {
            uint32_t l2base = l1 & 0xFFFFFC00u;
            uint32_t l2i    = (va >> 12) & 0xFF;
            uint32_t l2     = fetch_pa32(l2base + l2i * 4);
            uint32_t l2t    = l2 & 0x3;

            if (l2t == 0x2) {
                uint8_t dom   = (uint8_t)((l1 >> 5) & 0xF);
                uint8_t ap_lo = (uint8_t)((l2 >> 4) & 0x3);
                uint8_t ap_hi = (uint8_t)((l2 >> 8) & 0x3);
                uint8_t ap    = (uint8_t)(ap_lo | (ap_hi << 2));

                int acc = cp15.domain_access(dom);
                if (acc == 0) {
                    fault = true; fsr_code = MMU_FSR_DOMAIN_PAGE;
                    domain_faults++;
                    return do_fail(va, fetch, write, cp15, fsr_code);
                }
                if (acc == 1) {
                    if (!check_perm(ap, false, user, write)) {
                        fault = true; fsr_code = MMU_FSR_PERM_PAGE;
                        perm_faults++;
                        return do_fail(va, fetch, write, cp15, fsr_code);
                    }
                }
                uint32_t pa = (l2 & 0xFFFFF000u) | (va & 0xFFFu);
                tlb_add(va, pa, 0x1000, dom, ap, false);
                return pa;
            }
            // large page / invalid → page translation fault
            fault = true; fsr_code = MMU_FSR_TRANS_PAGE;
            return do_fail(va, fetch, write, cp15, fsr_code);
        }

        // type == 0x0 или 0x3 → section translation fault
        fault = true; fsr_code = MMU_FSR_TRANS_SECTION;
        return do_fail(va, fetch, write, cp15, fsr_code);
    }

    void dump() const {
        int used = 0;
        for (auto& e : tlb) if (e.valid) used++;
        std::printf("--- MMU ---\n");
        std::printf("  translations=%llu  faults=%llu (trans=%llu perm=%llu dom=%llu)\n",
            (unsigned long long)translations,
            (unsigned long long)faults,
            (unsigned long long)trans_faults,
            (unsigned long long)perm_faults,
            (unsigned long long)domain_faults);
        std::printf("  TLB: %d/%d used, hits=%llu misses=%llu\n",
            used, TLB_SIZE,
            (unsigned long long)tlb_hits,
            (unsigned long long)tlb_misses);
    }

    void dump_tlb() const {
        std::printf("--- TLB ---\n");
        int shown = 0;
        for (int i = 0; i < TLB_SIZE && shown < 32; i++) {
            if (tlb[i].valid) {
                std::printf("  [%3d] v=0x%08X p=0x%08X sz=0x%X dom=%u ap=0x%X %s\n",
                    i, tlb[i].vaddr_base, tlb[i].paddr_base,
                    tlb[i].size, tlb[i].domain, tlb[i].ap,
                    tlb[i].is_section ? "sec" : "pg");
                shown++;
            }
        }
        if (shown == 0) std::printf("  (empty)\n");
    }
};

#endif // ARM4_MMU_H
