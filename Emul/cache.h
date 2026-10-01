// cache.h — минимальный L1-кеш для ARM4-эмулятора.
// 2-канальный, 64 набора × 32 байта = 4 КБ.
// Сквозная запись, выделение при записи (write-allocate).
#ifndef ARM4_CACHE_H
#define ARM4_CACHE_H

#include <cstdint>
#include <cstring>
#include <cstdio>

class Cache {
public:
    static constexpr int SETS       = 64;
    static constexpr int WAYS       = 2;
    static constexpr int LINE_SIZE  = 32;
    static constexpr int LINE_SHIFT = 5;
    static constexpr int LINE_MASK  = LINE_SIZE - 1;
    static constexpr int SET_MASK   = SETS - 1;
    static constexpr int TAG_SHIFT  = LINE_SHIFT + 6;
    static constexpr int LINE_WORDS = LINE_SIZE / 4;

    struct Line {
        bool     valid = false;
        uint32_t tag   = 0;
        uint32_t lru   = 0;
        uint8_t  data[LINE_SIZE];
    };

    Line lines[SETS][WAYS];

    bool enabled = true;
    uint64_t hits = 0, misses = 0, evictions = 0;
    const char* name = "L1";

    uint8_t* mem = nullptr;
    size_t   mem_size = 0;

    Cache(const char* n = "L1") : name(n) {}
    void attach(uint8_t* m, size_t n) { mem = m; mem_size = n; }

    void reset_stats() { hits = misses = evictions = 0; }
    void invalidate_all() {
        for (auto& s : lines) for (auto& l : s) { l.valid = false; l.lru = 0; }
    }

    int      set_of(uint32_t a) const { return (a >> LINE_SHIFT) & SET_MASK; }
    uint32_t tag_of(uint32_t a) const { return  a >> TAG_SHIFT; }
    uint32_t line_base(uint32_t a) const { return a & ~(uint32_t)LINE_MASK; }
    int      byte_idx(uint32_t a) const { return a & LINE_MASK; }

    Line* find(uint32_t a) {
        int s = set_of(a);
        uint32_t t = tag_of(a);
        for (int w = 0; w < WAYS; w++) {
            if (lines[s][w].valid && lines[s][w].tag == t) {
                for (int k = 0; k < WAYS; k++)
                    if (k != w) lines[s][k].lru++;
                lines[s][w].lru = 0;
                return &lines[s][w];
            }
        }
        return nullptr;
    }

    Line* fetch(uint32_t a) {
        int s = set_of(a);
        uint32_t t = tag_of(a);
        uint32_t base = line_base(a);

        int victim = -1;
        for (int w = 0; w < WAYS; w++)
            if (!lines[s][w].valid) { victim = w; break; }
        if (victim < 0) {
            uint32_t best = 0;
            for (int w = 0; w < WAYS; w++)
                if (lines[s][w].lru >= best) { best = lines[s][w].lru; victim = w; }
            evictions++;
        }

        Line& L = lines[s][victim];
        L.valid = true;
        L.tag   = t;
        for (int k = 0; k < WAYS; k++) if (k != victim) lines[s][k].lru++;
        L.lru = 0;

        if (mem && base + LINE_SIZE <= mem_size) {
            std::memcpy(L.data, mem + base, LINE_SIZE);
        } else {
            for (int i = 0; i < LINE_SIZE; i++)
                L.data[i] = (mem && base + i < mem_size) ? mem[base + i] : 0;
        }
        return &L;
    }

    uint32_t read32(uint32_t a) {
        if (!enabled) {
            uint32_t v = 0;
            if (mem && a + 4 <= mem_size) std::memcpy(&v, mem + a, 4);
            return v;
        }
        Line* L = find(a);
        if (!L) { misses++; L = fetch(a); }
        else    hits++;
        uint32_t v;
        std::memcpy(&v, L->data + byte_idx(a), 4);
        return v;
    }
    uint16_t read16(uint32_t a) {
        uint32_t w = read32(a & ~3u);
        return (uint16_t)(w >> ((a & 2) * 8));
    }
    uint8_t read8(uint32_t a) {
        uint32_t w = read32(a & ~3u);
        return (uint8_t)(w >> ((a & 3) * 8));
    }

    void write32(uint32_t a, uint32_t v) {
        if (!enabled) {
            if (mem && a + 4 <= mem_size) std::memcpy(mem + a, &v, 4);
            return;
        }
        Line* L = find(a);
        if (L) {
            hits++;
        } else {
            misses++;
            L = fetch(a);   // write-allocate
        }
        std::memcpy(L->data + byte_idx(a), &v, 4);
        // Сквозная запись
        if (mem && a + 4 <= mem_size) std::memcpy(mem + a, &v, 4);
    }
    void write16(uint32_t a, uint16_t v) {
        uint32_t base = a & ~3u;
        uint32_t w = 0;
        if (mem && base + 4 <= mem_size) std::memcpy(&w, mem + base, 4);
        uint32_t sh = (a & 2) * 8;
        w = (w & ~(0xFFFFu << sh)) | ((uint32_t)v << sh);
        write32(base, w);
    }
    void write8(uint32_t a, uint8_t v) {
        uint32_t base = a & ~3u;
        uint32_t w = 0;
        if (mem && base + 4 <= mem_size) std::memcpy(&w, mem + base, 4);
        uint32_t sh = (a & 3) * 8;
        w = (w & ~(0xFFu << sh)) | ((uint32_t)v << sh);
        write32(base, w);
    }

    void dump() const {
        uint64_t tot = hits + misses;
        double hr = tot ? 100.0 * hits / tot : 0.0;
        std::printf("--- L1 Cache (%s) — %d-way, %d наборов × %d байт = %d байт ---\n",
                    name, WAYS, SETS, LINE_SIZE, SETS * WAYS * LINE_SIZE);
        std::printf("  enabled=%d  hits=%llu  misses=%llu  evictions=%llu  hit-rate=%.2f%%\n",
                    (int)enabled,
                    (unsigned long long)hits,
                    (unsigned long long)misses,
                    (unsigned long long)evictions,
                    hr);
    }
};

#endif
