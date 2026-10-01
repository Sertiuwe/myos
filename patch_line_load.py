import pathlib, re
p = pathlib.Path("ARM4.cpp")
src = p.read_text()
ok = True

# 1) Увеличить буферы командной строки 512 -> 4096
count = src.count('char line[512]')
src = src.replace('char line[512]', 'char line[4096]')
print(f"[+] line[512] -> line[4096] в {count} местах")

count = src.count('char line_str[512]')  # на всякий случай
src = src.replace('char line_str[512]', 'char line_str[4096]')

# 2) main-loop fgets
src = src.replace('if (!std::fgets(line, sizeof(line), stdin)) { std::printf("\\n"); break; }',
                  'if (!std::fgets(line, sizeof(line), stdin)) { std::printf("\\n"); break; }')
# уже sizeof — ок

# 3) в handler-е буфер
src = src.replace('char line[512];\n        std::strncpy(line, line_str.c_str(), sizeof(line) - 1);',
                  'char line[4096];\n        std::strncpy(line, line_str.c_str(), sizeof(line) - 1);')
# (если был такой шаблон)

# 4) runloop fgets
src = src.replace('char line[512];\n        uint64_t ticks = 0;',
                  'char line[4096];\n        uint64_t ticks = 0;')

# 5) Добавить команду vmcu loadfile
old = '''            if (std::strcmp(arg1, "load") == 0) {
                /* vmcu load <addr> <hex_words>'''
new = '''            if (std::strcmp(arg1, "loadfile") == 0) {
                /* vmcu loadfile <addr> <file>  : читает hex-файл, любая длина */
                char ma[64]={0}, mf[256]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %63s %255s", ma, mf);
                if (!ma[0] || !mf[0]) { std::fprintf(stderr, "vmcu loadfile <addr> <file>\\n"); return false; }
                uint32_t a = parse_u32(ma) & 0xFFFF;
                FILE* fp = std::fopen(mf, "r");
                if (!fp) { std::fprintf(stderr, "cannot open %s\\n", mf); return false; }
                std::vector<char> buf;
                buf.reserve(1<<20);
                char ch;
                while ((ch = std::fgetc(fp)) != EOF) {
                    if (ch != '\\n' && ch != '\\r' && ch != ' ' && ch != '\\t')
                        buf.push_back(ch);
                }
                std::fclose(fp);
                size_t len = buf.size();
                if (len == 0 || (len & 7)) {
                    std::fprintf(stderr, "bad hex length: %zu\\n", len); return false;
                }
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
                        int nib = hv(buf[i+k]);
                        if (nib < 0) { std::fprintf(stderr, "bad hex at %zu\\n", i+k); return false; }
                        word = (word << 4) | (uint32_t)nib;
                    }
                    if (a + words*4 + 4 > VMCU::MEM_SIZE) break;
                    for (int b = 0; b < 4; b++)
                        v.mem[(a + words*4 + b) & 0xFFFF] = (uint8_t)((word >> (b*8)) & 0xFF);
                    words++;
                }
                std::fprintf(stderr, "VMCU: загружено %d слов (%d байт) @ 0x%04X из %s\\n",
                            words, words*4, a, mf);
                return false;
            }
            if (std::strcmp(arg1, "load") == 0) {
                /* vmcu load <addr> <hex_words>'''
src = src.replace(old, new, 1)
print("[+] vmcu loadfile добавлен")

p.write_text(src)
print("OK")
