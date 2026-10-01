// virtrtc.h — PL031 Real-Time Clock для ARM4
//   MMIO 0x10130000, окно 0x1000
//   Возвращает unix time хоста + смещение
#ifndef ARM4_VIRTRTC_H
#define ARM4_VIRTRTC_H

#include <cstdint>
#include <cstdio>
#include <ctime>

class VirtRTC {
public:
    enum : uint32_t {
        RTCDR    = 0x000,   // Data — текущее время (R)
        RTCMR    = 0x004,   // Match (R/W)
        RTCLR    = 0x008,   // Load (W) — установить время
        RTCCR    = 0x00C,   // Control (R/W) — bit0 = enable
        RTCIMSC  = 0x010,   // Interrupt mask
        RTCRIS   = 0x014,   // Raw int status
        RTCMIS   = 0x018,   // Masked int status
        RTCICR   = 0x01C,   // Int clear
        PERIPH_ID0 = 0xFE0,
        PERIPH_ID1 = 0xFE4,
        PERIPH_ID2 = 0xFE8,
        PERIPH_ID3 = 0xFEC,
        PCELL_ID0  = 0xFF0,
        PCELL_ID1  = 0xFF4,
        PCELL_ID2  = 0xFF8,
        PCELL_ID3  = 0xFFC,
    };

    uint32_t base;
    const char* name;

    bool     enabled   = true;
    int64_t  offset    = 0;      // смещение от реального Unix time
    uint32_t match     = 0;
    uint32_t imsc      = 0;
    uint32_t ris       = 0;

    VirtRTC(uint32_t b, const char* n) : base(b), name(n) {}

    bool contains(uint32_t a) const { return a >= base && a < base + 0x1000; }

    uint32_t now() const {
        if (!enabled) return 0;
        time_t t = ::time(nullptr);
        return (uint32_t)((int64_t)t + offset);
    }

    uint32_t read(uint32_t addr, int width) {
        uint32_t off = addr - base;
        uint32_t v = 0;
        switch (off & ~3u) {
        case RTCDR:      v = now(); break;
        case RTCMR:      v = match; break;
        case RTCLR:      v = 0; break;
        case RTCCR:      v = enabled ? 1u : 0u; break;
        case RTCIMSC:    v = imsc; break;
        case RTCRIS:     v = ris; break;
        case RTCMIS:     v = ris & imsc; break;
        case RTCICR:     v = 0; break;
        case PERIPH_ID0: v = 0x31; break;
        case PERIPH_ID1: v = 0x10; break;
        case PERIPH_ID2: v = 0x04; break;
        case PERIPH_ID3: v = 0x00; break;
        case PCELL_ID0:  v = 0x0D; break;
        case PCELL_ID1:  v = 0xF0; break;
        case PCELL_ID2:  v = 0x05; break;
        case PCELL_ID3:  v = 0xB1; break;
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
        case RTCMR:  match = v; break;
        case RTCLR:
            {
                time_t t = ::time(nullptr);
                offset = (int64_t)(int32_t)v - (int64_t)t;
            }
            break;
        case RTCCR:  enabled = (v & 1) != 0; break;
        case RTCIMSC: imsc = v & 1; break;
        case RTCICR:  ris &= ~v; break;
        default: break;
        }
    }

    void tick() {}
};

#endif
