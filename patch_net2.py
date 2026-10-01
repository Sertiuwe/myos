import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''            if (std::strcmp(arg1, "send") == 0) {
                const char* p = line_str.c_str() + 4;
                while (*p == ' ') p++;
                emu.net.send_packet((const uint8_t*)p, std::strlen(p));
                return false;
            }
'''

new = '''            if (std::strcmp(arg1, "send") == 0) {
                /* Пропускаем "net send " — два слова */
                const char* p = line_str.c_str();
                while (*p && *p != ' ') p++;      // "net"
                while (*p == ' ') p++;
                while (*p && *p != ' ') p++;      // "send"
                while (*p == ' ') p++;
                size_t n = std::strlen(p);
                if (n == 0) { std::printf("net send <текст>\\n"); return false; }
                emu.net.tx_len += (uint32_t)n;
                if (emu.net.loopback) {
                    for (size_t i = 0; i < n; i++) emu.net.push_rx((uint8_t)p[i]);
                    emu.net.rx_len += (uint32_t)n;
                    emu.net.irq_stat |= 0x01;
                    std::printf("[NET] loopback %zu байт: '%s'\\n", n, p);
                } else {
                    emu.net.send_packet((const uint8_t*)p, n);
                }
                return false;
            }
'''

if old not in src:
    print("[!] шаблон не найден — уже пропатчено?")
else:
    src = src.replace(old, new, 1)
    print("[+] net send исправлен")

p.write_text(src)
print("готово")
