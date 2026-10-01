import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''            if (std::strcmp(arg1, "irq") == 0) {'''

new = '''            if (std::strcmp(arg1, "sendhex") == 0) {
                const char* p = line_str.c_str();
                while (*p && *p != ' ') p++;
                while (*p == ' ') p++;
                while (*p && *p != ' ') p++;
                while (*p == ' ') p++;
                size_t len = std::strlen(p);
                if (len == 0 || (len & 1)) { std::printf("net sendhex <hex>\\n"); return false; }
                std::vector<uint8_t> bin;
                bin.reserve(len / 2);
                for (size_t i = 0; i < len; i += 2) {
                    auto hexv = [](char c)->int{
                        if (c>='0'&&c<='9') return c-'0';
                        if (c>='a'&&c<='f') return c-'a'+10;
                        if (c>='A'&&c<='F') return c-'A'+10;
                        return -1;
                    };
                    int h = hexv(p[i]), l = hexv(p[i+1]);
                    if (h < 0 || l < 0) { std::printf("не hex: %c%c\\n", p[i], p[i+1]); return false; }
                    bin.push_back((uint8_t)((h<<4)|l));
                }
                emu.net.tx_len += (uint32_t)bin.size();
                if (emu.net.loopback) {
                    for (auto b : bin) emu.net.push_rx(b);
                    emu.net.rx_len += (uint32_t)bin.size();
                    emu.net.irq_stat |= 0x01;
                    std::printf("[NET] loopback %zu байт\\n", bin.size());
                } else {
                    emu.net.send_packet(bin.data(), bin.size());
                }
                return false;
            }
            if (std::strcmp(arg1, "rxhex") == 0) {
                std::printf("RX (%u байт): ", emu.net.rx_len);
                int n = 0;
                while (emu.net.rx_has() && n < 256) {
                    std::printf("%02X", emu.net.pop_rx());
                    n++;
                }
                std::printf("\\n");
                return false;
            }
            if (std::strcmp(arg1, "irq") == 0) {'''

if old not in src:
    print("[!] шаблон не найден")
else:
    src = src.replace(old, new, 1)
    print("[+] net sendhex / net rxhex добавлены")

p.write_text(src)
