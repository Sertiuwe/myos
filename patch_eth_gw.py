import pathlib
ok = True

# ---------- virteth.h ----------
p = pathlib.Path("virteth.h")
src = p.read_text()

# 1) поля gateway
old = '''    // Отладка
    bool sniff = false;
'''
new = '''    // Отладка
    bool sniff = false;

    // Виртуальный шлюз (ARP + ICMP responder)
    bool    gateway_enabled = true;
    uint8_t gw_mac[6] = {0x02,0x00,0x00,0x00,0x00,0xFE};
    uint8_t gw_ip[4]  = {10,0,0,1};
'''
if old in src:
    src = src.replace(old, new, 1); print("[+] virteth.h: поля gw добавлены")
else:
    print("[!] virteth.h: поля gw не найдены"); ok = False

# 2) в transmit_frame после loopback вызвать gateway_handle
old = '''        if (loopback) receive_frame(p, n);
        // сюда позже подключим внешний backend (сокеты)
'''
new = '''        if (loopback) { receive_frame(p, n); return; }
        gateway_handle(p, n);
'''
if old in src:
    src = src.replace(old, new, 1); print("[+] virteth.h: вызов gateway_handle")
else:
    print("[!] virteth.h: вызов gateway не найден"); ok = False

# 3) метод gateway_handle — вставляем перед методом read()
old = '''    uint32_t read(uint32_t addr, int width) {'''
new = r'''    // ---------- Виртуальный шлюз ----------
    static uint16_t ip_csum(const uint8_t* p, size_t n) {
        uint32_t s = 0;
        for (size_t i = 0; i + 1 < n; i += 2) s += ((uint32_t)p[i] << 8) | p[i+1];
        if (n & 1) s += ((uint32_t)p[n-1] << 8);
        while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
        return (uint16_t)~s;
    }

    void gateway_handle(const uint8_t* p, size_t n) {
        if (!gateway_enabled) return;
        if (n < 42) return;

        uint16_t etype = ((uint16_t)p[12] << 8) | p[13];

        // --- ARP ---
        if (etype == 0x0806 && n >= 42) {
            uint16_t oper = ((uint16_t)p[20] << 8) | p[21];
            if (oper != 1) return;  // только request
            // target IP = p[38..41]
            if (p[38] != gw_ip[0] || p[39] != gw_ip[1] ||
                p[40] != gw_ip[2] || p[41] != gw_ip[3]) return;
            uint8_t rsp[42];
            // Ethernet
            std::memcpy(rsp+0, p+6, 6);   // dst = наш (отправителя) MAC
            std::memcpy(rsp+6, gw_mac, 6); // src = gw MAC
            rsp[12]=0x08; rsp[13]=0x06;   // ARP
            // ARP
            rsp[14]=0x00; rsp[15]=0x01;   // htype
            rsp[16]=0x08; rsp[17]=0x00;   // ptype
            rsp[18]=6;    rsp[19]=4;
            rsp[20]=0x00; rsp[21]=0x02;   // reply
            std::memcpy(rsp+22, gw_mac, 6);
            std::memcpy(rsp+28, gw_ip, 4);
            std::memcpy(rsp+32, p+22, 6);  // tha = orig sha
            std::memcpy(rsp+38, p+28, 4);  // tpa = orig spa
            std::printf("[ETH GW] ARP reply -> %u.%u.%u.%u is at gw\n",
                        p[28],p[29],p[30],p[31]);
            // кладём в RX
            if (rx_queue.size() < QMAX) {
                rx_queue.push_back(Frame{ std::vector<uint8_t>(rsp, rsp+42) });
                stat_rx_ok++;
                stat_rx_bytes += 42;
                irq_stat |= 0x01;
                sniff_dump("RX", rsp, 42);
            }
            return;
        }

        // --- IPv4 / ICMP ---
        if (etype == 0x0800 && n >= 42) {
            if ((p[14] >> 4) != 4) return;
            size_t ihl = (p[14] & 0x0F) * 4;
            if (ihl < 20 || n < 14 + ihl + 8) return;
            uint8_t proto = p[14 + 9];
            // dst IP
            if (p[14+16] != gw_ip[0] || p[14+17] != gw_ip[1] ||
                p[14+18] != gw_ip[2] || p[14+19] != gw_ip[3]) return;
            if (proto != 1) return;  // ICMP only
            uint8_t* icmp = (uint8_t*)p + 14 + ihl;
            if (icmp[0] != 8) return;  // echo request

            std::vector<uint8_t> rsp(p, p+n);
            // Ethernet: поменять src/dst
            std::memcpy(rsp.data()+0, p+6, 6);
            std::memcpy(rsp.data()+6, gw_mac, 6);
            // IP: поменять src/dst
            uint8_t* ip = rsp.data()+14;
            uint8_t tmp[4];
            std::memcpy(tmp, ip+12, 4); std::memcpy(ip+12, ip+16, 4); std::memcpy(ip+16, tmp, 4);
            // пересчёт IP checksum
            ip[10]=0; ip[11]=0;
            uint16_t ic = ip_csum(ip, ihl);
            ip[10]=(uint8_t)(ic>>8); ip[11]=(uint8_t)(ic & 0xFF);
            // ICMP: type 8->0, пересчёт checksum
            uint8_t* ic_ = rsp.data()+14+ihl;
            ic_[0]=0;
            ic_[2]=0; ic_[3]=0;
            size_t icmp_len = n - 14 - ihl;
            uint16_t cc = ip_csum(ic_, icmp_len);
            ic_[2]=(uint8_t)(cc>>8); ic_[3]=(uint8_t)(cc & 0xFF);

            std::printf("[ETH GW] ICMP echo reply, %zu байт\n", rsp.size());
            if (rx_queue.size() < QMAX) {
                rx_queue.push_back(Frame{ rsp });
                stat_rx_ok++;
                stat_rx_bytes += (uint32_t)rsp.size();
                irq_stat |= 0x01;
                sniff_dump("RX", rsp.data(), rsp.size());
            }
            return;
        }
    }

    uint32_t read(uint32_t addr, int width) {'''
if old in src:
    src = src.replace(old, new, 1); print("[+] virteth.h: gateway_handle добавлен")
else:
    print("[!] virteth.h: gateway_handle не вставлен"); ok = False

p.write_text(src)

# ---------- ARM4.cpp: команды eth gw ----------
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''            if (std::strcmp(arg1, "sniff") == 0) {'''
new = '''            if (std::strcmp(arg1, "gw") == 0) {
                e.gateway_enabled = arg2[0] ? (std::atoi(arg2)!=0) : !e.gateway_enabled;
                std::printf("gateway_enabled=%d\\n", (int)e.gateway_enabled);
                return false;
            }
            if (std::strcmp(arg1, "gwip") == 0) {
                unsigned a,b,c,d;
                if (arg2[0] && std::sscanf(arg2, "%u.%u.%u.%u", &a,&b,&c,&d) == 4) {
                    e.gw_ip[0]=a; e.gw_ip[1]=b; e.gw_ip[2]=c; e.gw_ip[3]=d;
                }
                std::printf("gw_ip = %u.%u.%u.%u\\n", e.gw_ip[0],e.gw_ip[1],e.gw_ip[2],e.gw_ip[3]);
                return false;
            }
            if (std::strcmp(arg1, "gwmac") == 0) {
                unsigned a,b,c,d,f,g;
                if (arg2[0] && std::sscanf(arg2, "%x:%x:%x:%x:%x:%x", &a,&b,&c,&d,&f,&g) == 6) {
                    e.gw_mac[0]=a; e.gw_mac[1]=b; e.gw_mac[2]=c;
                    e.gw_mac[3]=d; e.gw_mac[4]=f; e.gw_mac[5]=g;
                }
                std::printf("gw_mac = %02X:%02X:%02X:%02X:%02X:%02X\\n",
                    e.gw_mac[0],e.gw_mac[1],e.gw_mac[2],e.gw_mac[3],e.gw_mac[4],e.gw_mac[5]);
                return false;
            }
            if (std::strcmp(arg1, "sniff") == 0) {'''
if old in src:
    src = src.replace(old, new, 1); print("[+] ARM4.cpp: команды eth gw/gwip/gwmac добавлены")
else:
    print("[!] ARM4.cpp: anchor для eth gw не найден"); ok = False

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
