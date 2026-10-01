// virtwdog.h — SP805 Watchdog для ARM4
//   MMIO 0x101E4000, окно 0x1000
//   v1: печатает warning, не ресетит эмулятор
//   locked bit — как у настоящего PL805 (magic 0x1ACCE551)
#ifndef ARM4_VIRTWDOG_H
#define ARM4_VIRTWDOG_H

#include <cstdint>
#include <cstdio>

class VirtWatchdog {
public:
    enum : uint32_t {
        WDOGLOAD    = 0x000,   // R/W — значение загрузки
        WDOGVALUE   = 0x004,   // R   — текущее значение
        WDOGCONTROL = 0x008,   // R/W — bit0=INTEN, bit1=RESEN
        WDOGINTCLR  = 0x00C,   // W   — clear IRQ + reload
        WDOGRIS     = 0x010,   // R
        WDOGMIS     = 0x014,   // R
        WDOGLOCK    = 0xC00,   // R/W — magic unlock
        PERIPH_ID0  = 0xFE0,
        PERIPH_ID1  = 0xFE4,
        PERIPH_ID2  = 0xFE8,
        PERIPH_ID3  = 0xFEC,
    };

    static constexpr uint32_t UNLOCK_MAGIC = 0x1ACCE551u;

    uint32_t base;
    const char* name;

    uint32_t load      = 0xFFFFFFFFu;
    uint32_t value     = 0xFFFFFFFFu;
    bool     int_en    = false;
    bool     res_en    = false;
    bool     locked    = true;
    uint32_t ris       = 0;
    uint32_t tick_div  = 0;
    uint32_t fired_count = 0;

    VirtWatchdog(uint32_t b, const char* n) : base(b), name(n) {}

    bool contains(uint32_t a) const { return a >= base && a < base + 0x1000; }

    uint32_t read(uint32_t addr, int width) {
        uint32_t off = addr - base;
        uint32_t v = 0;
        switch (off & ~3u) {
        case WDOGLOAD:    v = load; break;
        case WDOGVALUE:   v = value; break;
        case WDOGCONTROL: v = (int_en ? 1u : 0u) | (res_en ? 2u : 0u); break;
        case WDOGRIS:     v = ris; break;
        case WDOGMIS:     v = ris & (int_en ? 1u : 0u); break;
        case WDOGLOCK:    v = locked ? 1u : 0u; break;
        case PERIPH_ID0:  v = 0x05; break;
        case PERIPH_ID1:  v = 0x18; break;
        case PERIPH_ID2:  v = 0x24; break;
        case PERIPH_ID3:  v = 0x00; break;
        default: v = 0;
        }
        if (width == 2) v = (v >> ((addr & 2) * 8)) & 0xFFFFu;
        else if (width == 1) v = (v >> ((addr & 3) * 8)) & 0xFFu;
        return v;
    }

    void write(uint32_t addr, uint32_t v, int width) {
        (void)width;
        uint32_t off = addr - base;
        switch (off & ~3u) {
        case WDOGLOCK:
            locked = (v != UNLOCK_MAGIC);
            break;
        case WDOGLOAD:
            if (locked) break;
            load  = v;
            value = v;
            break;
        case WDOGCONTROL:
            if (locked) break;
            int_en = (v & 1) != 0;
            res_en = (v & 2) != 0;
            break;
        case WDOGINTCLR:
            // даже при locked — очистка разрешена (так у PL805)
            ris   = 0;
            value = load;
            break;
        default: break;
        }
    }

    void tick() {
        if (locked) return;
        if (!int_en && !res_en) return;

        // 1 тик watchdog = 4 тика ARM (как у нас для VMCU)
        if (++tick_div < 4) return;
        tick_div = 0;

        if (value > 0) {
            value--;
            return;
        }
        // Сработал!
        ris = 1;
        if (res_en) {
            fired_count++;
            std::printf("\n*** WATCHDOG FIRED at value=0, #%u ***\n", fired_count);
            std::printf("*** (reset disabled in v1 — emulator continues) ***\n\n");
            value = load;   // перезагрузить
        }
    }
};

#endif
