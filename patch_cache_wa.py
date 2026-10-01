import pathlib
p = pathlib.Path("cache.h")
src = p.read_text()

old = '''    void write32(uint32_t a, uint32_t v) {
        if (!enabled) {
            if (mem && a + 4 <= mem_size) std::memcpy(mem + a, &v, 4);
            return;
        }
        Line* L = find(a);
        if (L) {
            hits++;
            std::memcpy(L->data + byte_idx(a), &v, 4);
        } else {
            misses++;
        }
        // Сквозная запись
        if (mem && a + 4 <= mem_size) std::memcpy(mem + a, &v, 4);
    }'''

new = '''    void write32(uint32_t a, uint32_t v) {
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
    }'''

if old in src:
    src = src.replace(old, new, 1)
    print("[+] write-allocate")
else:
    print("[!] не найден write32")
p.write_text(src)
