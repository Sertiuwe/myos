import pathlib

# --- virtlcd.h: fix BPP display ---
p = pathlib.Path("virtlcd.h")
src = p.read_text()
old = '''        std::printf("  Enabled=%d  BPP=%d  Size=%dx%d  UPBASE=0x%08X  LPBASE=0x%08X\\n",
                    (int)((control & CNTL_LCDEN) != 0), width, height, upbase, lpbase);'''
# actual current line (single line in file)
old_line = None
for line in src.split('\n'):
    if 'Enabled=%d' in line and 'BPP=%d' in line:
        old_line = line
        break

if old_line:
    # Определим bpp через switch
    new_block = '''        int _bpp = 16;
        switch ((control >> 1) & 0x7) {
            case 0: _bpp = 1; break; case 1: _bpp = 2; break;
            case 2: _bpp = 4; break; case 3: _bpp = 8; break;
            case 4: _bpp = 16; break; case 5: _bpp = 24; break;
            case 6: _bpp = 32; break; default: _bpp = 16;
        }
        std::fprintf(stderr, "  Enabled=%d  BPP=%d  Size=%dx%d  UPBASE=0x%08X  LPBASE=0x%08X\\n",
                    (int)((control & CNTL_LCDEN) != 0), _bpp, width, height, upbase, lpbase);'''
    # Найти две строки: std::printf("  Enabled=%d ... width, height, upbase, lpbase);
    # Заменим обе целиком
    two = old_line + '\n'
    # find the next line too (arguments on next line)
    idx = src.find(old_line)
    if idx >= 0:
        # capture 2 lines (call ends with ')
        end = src.find(');', idx) + 2
        chunk = src[idx:end]
        src = src[:idx] + new_block + src[end:]
        print("[+] virtlcd.h: BPP display fix")
    # Также все dump printf → stderr
    src = src.replace('std::printf("  IMSC=0x%X', 'std::fprintf(stderr, "  IMSC=0x%X')
    src = src.replace('std::printf("  Renders=%llu', 'std::fprintf(stderr, "  Renders=%llu')
    src = src.replace('std::printf("  Backend=', 'std::fprintf(stderr, "  Backend=')
    src = src.replace('std::printf("--- PL111 LCD', 'std::fprintf(stderr, "--- PL111 LCD')
    p.write_text(src)

# --- ARM4.cpp: shell LCD: printf → stderr, remove init_term from fb ---
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

fixes = [
    # fb: убрать init_term, статус в stderr
    ('                L.upbase = parse_u32(arg2) & 0xFFFFFFF0u;\n'
     '                L.init_term();\n'
     '                L.control |= VirtLCD::CNTL_LCDEN | VirtLCD::CNTL_LCDBPP_16 | VirtLCD::CNTL_LCDTFT;\n'
     '                std::printf("LCD FB=0x%08X, 320x240 RGB565\\n", L.upbase);\n'
     '                return false;',
     '                L.upbase = parse_u32(arg2) & 0xFFFFFFF0u;\n'
     '                L.control |= VirtLCD::CNTL_LCDEN | VirtLCD::CNTL_LCDBPP_16 | VirtLCD::CNTL_LCDTFT;\n'
     '                std::fprintf(stderr, "LCD FB=0x%08X, 320x240 RGB565\\n", L.upbase);\n'
     '                return false;'),
    # on: убрать init_term (только control)
    ('                L.init_term();\n'
     '                L.control |= VirtLCD::CNTL_LCDEN | VirtLCD::CNTL_LCDBPP_16 | VirtLCD::CNTL_LCDTFT;\n'
     '                std::printf("LCD on\\n");',
     '                L.control |= VirtLCD::CNTL_LCDEN | VirtLCD::CNTL_LCDBPP_16 | VirtLCD::CNTL_LCDTFT;\n'
     '                std::fprintf(stderr, "LCD on\\n");'),
    # render
    ('                L.render();\n'
     '                std::printf("LCD rendered (frames=%llu)\\n", (unsigned long long)L.renders);',
     '                if (!L.nc_ready && !L.ansi_only) L.init_term();\n'
     '                L.render();\n'
     '                std::fprintf(stderr, "LCD rendered (frames=%llu)\\n", (unsigned long long)L.renders);'),
    # off
    ('                L.control &= ~VirtLCD::CNTL_LCDEN;\n'
     '                L.shutdown();\n'
     '                std::printf("LCD off\\n");',
     '                L.control &= ~VirtLCD::CNTL_LCDEN;\n'
     '                L.shutdown();\n'
     '                std::fprintf(stderr, "LCD off\\n");'),
    # gradient
    ('                if (!L.upbase) { std::printf("lcd fb <addr> \\u0441\\u043d\\u0430\\u0447\\u0430\\u043b\\u0430\\n"); return false; }',
     '                if (!L.upbase) { std::fprintf(stderr, "lcd fb <addr> first\\n"); return false; }\n'
     '                if (!L.nc_ready && !L.ansi_only) L.init_term();'),
    ('                L.render();\n'
     '                std::printf("LCD gradient, frames=%llu\\n", (unsigned long long)L.renders);',
     '                L.render();\n'
     '                std::fprintf(stderr, "LCD gradient, frames=%llu\\n", (unsigned long long)L.renders);'),
    # checker
    ('                if (!L.upbase) { std::printf("lcd fb <addr>\\n"); return false; }',
     '                if (!L.upbase) { std::fprintf(stderr, "lcd fb <addr> first\\n"); return false; }\n'
     '                if (!L.nc_ready && !L.ansi_only) L.init_term();'),
    ('                L.render();\n'
     '                std::printf("LCD checker, frames=%llu\\n", (unsigned long long)L.renders);',
     '                L.render();\n'
     '                std::fprintf(stderr, "LCD checker, frames=%llu\\n", (unsigned long long)L.renders);'),
    # fill
    ('                if (!L.upbase) { std::printf("lcd fb <addr> \\u0441\\u043d\\u0430\\u0447\\u0430\\u043b\\u0430\\n"); return false; }',
     '                if (!L.upbase) { std::fprintf(stderr, "lcd fb <addr> first\\n"); return false; }\n'
     '                if (!L.nc_ready && !L.ansi_only) L.init_term();'),
    ('                std::printf("LCD fill 0x%04X, frames=%llu\\n",\n'
     '                            c, (unsigned long long)L.renders);',
     '                std::fprintf(stderr, "LCD fill 0x%04X, frames=%llu\\n",\n'
     '                            c, (unsigned long long)L.renders);'),
]

for i, (old, new) in enumerate(fixes):
    if old in src:
        if src.count(old) == 1:
            src = src.replace(old, new, 1)
            print(f"[+] fix {i}")
        else:
            print(f"[!] fix {i}: неоднозначно ({src.count(old)})")
    else:
        print(f"[!] fix {i}: не найдено")

p.write_text(src)
print("OK")
