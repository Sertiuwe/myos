import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''            if (std::strcmp(arg1, "load") == 0) {
                const char* pp = line_str.c_str();
                while (*pp && *pp != ' ') pp++; while (*pp == ' ') pp++;
                while (*pp && *pp != ' ') pp++; while (*pp == ' ') pp++;
                char addr_s[32]={0}; int ai=0;
                while (*pp && *pp != ' ' && ai < 31) addr_s[ai++] = *pp++;
                while (*pp == ' ') pp++;
                uint32_t a = parse_u32(addr_s) & 0xFFFF;
                size_t len = std::strlen(pp);
                if (len == 0 || (len & 1)) { std::printf("vmcu load <addr> <hex>\\n"); return false; }
                auto hv = [](char c)->int {
                    if (c>='0'&&c<='9') return c-'0';
                    if (c>='a'&&c<='f') return c-'a'+10;
                    if (c>='A'&&c<='F') return c-'A'+10;
                    return -1;
                };
                int written = 0;
                for (size_t i = 0; i < len; i += 2) {
                    int h = hv(pp[i]), l = hv(pp[i+1]);
                    if (h < 0 || l < 0) { std::printf("не hex\\n"); return false; }
                    if (a + written >= VMCU::MEM_SIZE) break;
                    v.mem[(a + written) & 0xFFFF] = (uint8_t)((h << 4) | l);
                    written++;
                }
                std::printf("VMCU: загружено %d байт @ 0x%04X\\n", written, a);
                return false;
            }'''

new = '''            if (std::strcmp(arg1, "load") == 0) {
                /* vmcu load <addr> <hex_words>
                 * Каждые 8 hex-символов = одно 32-битное слово в big-endian
                 * (человеческий вид). В память кладётся little-endian. */
                const char* pp = line_str.c_str();
                while (*pp && *pp != ' ') pp++; while (*pp == ' ') pp++;
                while (*pp && *pp != ' ') pp++; while (*pp == ' ') pp++;
                char addr_s[32]={0}; int ai=0;
                while (*pp && *pp != ' ' && ai < 31) addr_s[ai++] = *pp++;
                while (*pp == ' ') pp++;
                size_t len = std::strlen(pp);
                if (len == 0 || (len & 7)) {
                    std::printf("vmcu load <addr> <8hex * N>\\n");
                    std::printf("  пример: vmcu load 0 5D0000035D10000450201000\\n");
                    return false;
                }
                uint32_t a = parse_u32(addr_s) & 0xFFFF;
                auto hv = [](char c)->int {
                    if (c>='0'&&c<='9') return c-'0';
                    if (c>='a'&&c<='f') return c-'a'+10;
                    if (c>='A'&&c<='F') return c-'A'+10;
                    return -1;
                };
                int words = 0;
                for (size_t i = 0; i < len; i += 8) {
                    uint32_t word = 0;
                    for (int k = 0; k < 8; k++) {
                        int nib = hv(pp[i+k]);
                        if (nib < 0) { std::printf("не hex @%zu\\n", i+k); return false; }
                        word = (word << 4) | (uint32_t)nib;
                    }
                    if (a + words*4 + 4 > VMCU::MEM_SIZE) break;
                    for (int b = 0; b < 4; b++)
                        v.mem[(a + words*4 + b) & 0xFFFF] = (uint8_t)((word >> (b*8)) & 0xFF);
                    words++;
                }
                std::printf("VMCU: загружено %d слов (%d байт) @ 0x%04X\\n",
                            words, words*4, a);
                return false;
            }'''

if old not in src:
    print("[!] шаблон vmcu load не найден")
    raise SystemExit(1)
src = src.replace(old, new, 1)
p.write_text(src)
print("[+] vmcu load: приём 32-битных слов в big-endian виде")
