import pathlib

# --- virtlcd.h: default = inline ANSI; notcurses только если prefer_notcurses ---
p = pathlib.Path("virtlcd.h")
src = p.read_text()

# 1) добавить флаг prefer_notcurses
old = '''    bool ansi_only = false;
    bool ansi_clear_done = false;'''
new = '''    bool ansi_only = false;
    bool ansi_clear_done = false;
    bool prefer_notcurses = false;   // включить notcurses (иначе inline ANSI)'''
if old in src and 'prefer_notcurses' not in src:
    src = src.replace(old, new, 1)
    print("[+] prefer_notcurses")

# 2) init_term: если !prefer_notcurses — форсим ansi_only
old = '''    bool init_term() {
#ifdef HAVE_NOTCURSES
        if (nc_ready) return true;
        setlocale(LC_ALL, "");'''
new = '''    bool init_term() {
        if (!prefer_notcurses) {
            ansi_only = true;
            term_rows = 24; term_cols = 80;
            return true;
        }
#ifdef HAVE_NOTCURSES
        if (nc_ready) return true;
        setlocale(LC_ALL, "");'''
if old in src:
    src = src.replace(old, new, 1)
    print("[+] init_term с inline ANSI")

# 3) render: в ansi режиме не стирать экран каждый раз (у нас уже cursor home)
old = '''        if (!ansi_clear_done) {
            std::printf("\\033[2J");
            ansi_clear_done = true;
        }'''
new = '''        // Первый рендер — очистить экран целиком и спрятать курсор
        if (!ansi_clear_done) {
            std::printf("\\033[2J\\033[?25l");
            ansi_clear_done = true;
        }'''
if old in src:
    src = src.replace(old, new, 1)
    print("[+] ansi: clear + hide cursor")

# 4) shutdown: в ansi режиме вернуть курсор
old = '''        if (ansi_only && ansi_clear_done) {
            std::printf("\\033[2J\\033[H");
            std::fflush(stdout);
            ansi_clear_done = false;
        }'''
new = '''        if (ansi_only && ansi_clear_done) {
            std::printf("\\033[?25h\\033[H\\n");   // показать курсор, домой
            std::fflush(stdout);
            ansi_clear_done = false;
        }'''
if old in src:
    src = src.replace(old, new, 1)
    print("[+] ansi: restore cursor on shutdown")

p.write_text(src)

# --- ARM4.cpp: lcd nc on|off + первый рендер через init_term ---
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

# Найти блок lcd
start = src.find('        if (std::strcmp(cmd, "lcd") == 0) {')
end = src.find('        if (std::strcmp(cmd,', start + 100)
if start < 0 or end < 0:
    print("[!] lcd-блок не найден"); raise SystemExit(1)

new_block = '''        if (std::strcmp(cmd, "lcd") == 0) {
            VirtLCD& L = emu.lcd;
            if (!arg1[0] || std::strcmp(arg1, "dump") == 0) { L.dump(); return false; }
            if (std::strcmp(arg1, "nc") == 0) {
                L.prefer_notcurses = arg2[0] ? (std::atoi(arg2) != 0) : !L.prefer_notcurses;
                std::fprintf(stderr, "lcd backend = %s\\n",
                             L.prefer_notcurses ? "notcurses" : "inline-ANSI");
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
                L.init_term();
                L.last_hash = 0;
                L.render();
                std::fprintf(stderr, "LCD rendered, frames=%llu\\n", (unsigned long long)L.renders);
                return false;
            }
            if (std::strcmp(arg1, "fill") == 0) {
                if (!arg2[0]) { std::fprintf(stderr, "lcd fill <rgb565>\\n"); return false; }
                uint16_t c = (uint16_t)parse_u32(arg2);
                for (int y = 0; y < 240; y++)
                    for (int x = 0; x < 320; x++)
                        std::memcpy(emu.mem.data.data() + L.upbase + y*640 + x*2, &c, 2);
                L.init_term();
                L.last_hash = 0;
                L.render();
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
                L.init_term();
                L.last_hash = 0;
                L.render();
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
                L.init_term();
                L.last_hash = 0;
                L.render();
                std::fprintf(stderr, "LCD checker, frames=%llu\\n",
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
            std::fprintf(stderr, "lcd dump|on|off|nc [0|1]|render|fb <a>|fill <c>|pixel <x> <y> <c>|gradient|checker\\n");
            return false;
        }
'''
src = src[:start] + new_block + src[end:]
p.write_text(src)
print("[+] lcd-блок перезаписан с inline/notcurses")
