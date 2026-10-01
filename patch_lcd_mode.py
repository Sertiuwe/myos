import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

start = src.find('        if (std::strcmp(cmd, "lcd") == 0) {')
if start < 0:
    print("[!] lcd-блок не найден"); raise SystemExit(1)
end = src.find('        if (std::strcmp(cmd,', start + 100)
if end < 0:
    print("[!] следующий cmd не найден"); raise SystemExit(1)

new_block = '''        if (std::strcmp(cmd, "lcd") == 0) {
            VirtLCD& L = emu.lcd;
            if (!arg1[0] || std::strcmp(arg1, "dump") == 0) { L.dump(); return false; }
            if (std::strcmp(arg1, "mode") == 0) {
                if (!arg2[0]) {
                    const char* m[] = {"ascii","truecolor","256color","notcurses"};
                    std::fprintf(stderr, "lcd mode = %s\\n", m[L.mode]);
                    return false;
                }
                if      (std::strcmp(arg2, "ascii")     == 0) L.mode = VirtLCD::MODE_ASCII;
                else if (std::strcmp(arg2, "truecolor") == 0) L.mode = VirtLCD::MODE_TRUECOLOR;
                else if (std::strcmp(arg2, "256")       == 0 ||
                         std::strcmp(arg2, "256color")  == 0) L.mode = VirtLCD::MODE_NC256;
                else if (std::strcmp(arg2, "nc")        == 0 ||
                         std::strcmp(arg2, "notcurses") == 0) L.mode = VirtLCD::MODE_NOTCURSES;
                else { std::fprintf(stderr, "lcd mode ascii|truecolor|256|nc\\n"); return false; }
                L.last_hash = 0;
                std::fprintf(stderr, "lcd mode set\\n");
                return false;
            }
            if (std::strcmp(arg1, "on") == 0) {
                L.init_term();
                L.control |= VirtLCD::CNTL_LCDEN | VirtLCD::CNTL_LCDBPP_16 | VirtLCD::CNTL_LCDTFT;
                std::fprintf(stderr, "LCD on\\n");
                return false;
            }
            if (std::strcmp(arg1, "off") == 0) {
                L.control &= ~VirtLCD::CNTL_LCDEN;
                L.shutdown();
                std::fprintf(stderr, "LCD off\\n");
                return false;
            }
            if (std::strcmp(arg1, "fb") == 0) {
                if (!arg2[0]) { std::fprintf(stderr, "lcd fb <addr>\\n"); return false; }
                L.upbase = parse_u32(arg2) & 0xFFFFFFF0u;
                L.control |= VirtLCD::CNTL_LCDEN | VirtLCD::CNTL_LCDBPP_16 | VirtLCD::CNTL_LCDTFT;
                L.last_hash = 0;
                std::fprintf(stderr, "LCD FB=0x%08X, 320x240 RGB565\\n", L.upbase);
                return false;
            }
            if (!L.upbase) { std::fprintf(stderr, "\\u0441\\u043d\\u0430\\u0447\\u0430\\u043b\\u0430: lcd fb <addr>\\n"); return false; }
            if (std::strcmp(arg1, "render") == 0) {
                L.init_term(); L.last_hash = 0; L.render();
                std::fprintf(stderr, "LCD rendered, frames=%llu\\n", (unsigned long long)L.renders);
                return false;
            }
            if (std::strcmp(arg1, "fill") == 0) {
                if (!arg2[0]) { std::fprintf(stderr, "lcd fill <rgb565>\\n"); return false; }
                uint16_t c = (uint16_t)parse_u32(arg2);
                for (int y = 0; y < 240; y++)
                    for (int x = 0; x < 320; x++)
                        std::memcpy(emu.mem.data.data() + L.upbase + y*640 + x*2, &c, 2);
                L.init_term(); L.last_hash = 0; L.render();
                std::fprintf(stderr, "LCD fill 0x%04X, frames=%llu\\n",
                            c, (unsigned long long)L.renders);
                return false;
            }
            if (std::strcmp(arg1, "gradient") == 0) {
                for (int y = 0; y < 240; y++)
                    for (int x = 0; x < 320; x++) {
                        int r = x * 31 / 319;
                        int g = y * 63 / 239;
                        int b = 31 - (x * 31 / 319);
                        uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
                        std::memcpy(emu.mem.data.data() + L.upbase + y*640 + x*2, &c, 2);
                    }
                L.init_term(); L.last_hash = 0; L.render();
                std::fprintf(stderr, "LCD gradient, frames=%llu\\n",
                            (unsigned long long)L.renders);
                return false;
            }
            if (std::strcmp(arg1, "checker") == 0) {
                for (int y = 0; y < 240; y++)
                    for (int x = 0; x < 320; x++) {
                        int cell = ((x/20) ^ (y/20)) & 1;
                        uint16_t c = cell ? 0xFFFF : 0x0000;
                        std::memcpy(emu.mem.data.data() + L.upbase + y*640 + x*2, &c, 2);
                    }
                L.init_term(); L.last_hash = 0; L.render();
                std::fprintf(stderr, "LCD checker, frames=%llu\\n",
                            (unsigned long long)L.renders);
                return false;
            }
            if (std::strcmp(arg1, "bars") == 0) {
                /* test pattern: 7 цветных вертикальных полос (SMPTE-ish) */
                static const uint16_t bars[7] = {
                    0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F
                };
                for (int y = 0; y < 240; y++)
                    for (int x = 0; x < 320; x++) {
                        int b = x * 7 / 320;
                        uint16_t c = bars[b];
                        std::memcpy(emu.mem.data.data() + L.upbase + y*640 + x*2, &c, 2);
                    }
                L.init_term(); L.last_hash = 0; L.render();
                std::fprintf(stderr, "LCD bars, frames=%llu\\n",
                            (unsigned long long)L.renders);
                return false;
            }
            if (std::strcmp(arg1, "pixel") == 0) {
                char xs[16]={0}, ys[16]={0}, cs[16]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %15s %15s %15s", xs, ys, cs);
                if (!xs[0] || !ys[0] || !cs[0]) {
                    std::fprintf(stderr, "lcd pixel <x> <y> <rgb565>\\n"); return false;
                }
                int x = std::atoi(xs), y = std::atoi(ys);
                uint16_t c = (uint16_t)parse_u32(cs);
                if (x < 0 || x >= 320 || y < 0 || y >= 240) return false;
                std::memcpy(emu.mem.data.data() + L.upbase + y*640 + x*2, &c, 2);
                return false;
            }
            std::fprintf(stderr, "lcd dump|mode [ascii|truecolor|256|nc]|on|off|render|fb <a>|fill <c>|pixel <x> <y> <c>|gradient|checker|bars\\n");
            return false;
        }
'''
src = src[:start] + new_block + src[end:]
p.write_text(src)
print("[+] lcd-блок с mode + bars + colors")
