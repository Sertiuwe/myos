import pathlib

p = pathlib.Path("virteth.h")
src = p.read_text()
ok = True

# 1) sockets в virteth.h
if '#include <sys/socket.h>' not in src:
    old = '#include <deque>\n'
    new = ('#include <deque>\n'
           '#if defined(__unix__) || defined(__ANDROID__)\n'
           '  #include <sys/socket.h>\n'
           '  #include <netinet/in.h>\n'
           '  #include <arpa/inet.h>\n'
           '  #include <fcntl.h>\n'
           '  #include <unistd.h>\n'
           '  #include <errno.h>\n'
           '  #define VIRTETH_HAVE_SOCK 1\n'
           '#endif\n')
    src = src.replace(old, new, 1); print("[+] include sockets")
else:
    print("[=] include sockets уже есть")

# 2) поле udp_bridge_sock + init
old = '''    // Отладка
    bool sniff = false;
'''
new = '''    // Отладка
    bool sniff = false;

    // UDP-bridge (NAT в реальный UDP)
    int  udp_bridge_sock = -1;
    bool udp_bridge_on = false;
    uint32_t udp_bridge_tx = 0, udp_bridge_rx = 0;
'''
if old in src and 'udp_bridge_sock' not in src:
    src = src.replace(old, new, 1); print("[+] поля udp_bridge")
elif 'udp_bridge_sock' in src:
    print("[=] поля udp_bridge уже есть")
else:
    print("[!] не найдено поле sniff"); ok = False

# 3) методы bridge — вставляем перед gateway_handle
old = '''    // ---------- Виртуальный шлюз ----------'''
new = r'''    // ---------- UDP-мост в реальную сеть ----------
    bool udp_bridge_open() {
#ifdef VIRTETH_HAVE_SOCK
        if (udp_bridge_sock >= 0) return true;
        udp_bridge_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_bridge_sock < 0) { std::perror("[ETH-BRIDGE] socket"); return false; }
        int fl = ::fcntl(udp_bridge_sock, F_GETFL, 0);
        ::fcntl(udp_bridge_sock, F_SETFL, fl | O_NONBLOCK);
        udp_bridge_on = true;
        std::printf("[ETH-BRIDGE] UDP-мост открыт\n");
        return true;
#else
        std::printf("[ETH-BRIDGE] сокеты недоступны\n");
        return false;
#endif
    }
    void udp_bridge_close() {
#ifdef VIRTETH_HAVE_SOCK
        if (udp_bridge_sock >= 0) { ::close(udp_bridge_sock); udp_bridge_sock = -1; }
        udp_bridge_on = false;
#endif
    }

    // Отправка UDP payload из MyOS в реальный мир
    void udp_bridge_send(const uint8_t* ip_pkt, size_t ip_len) {
#ifdef VIRTETH_HAVE_SOCK
        if (!udp_bridge_on || udp_bridge_sock < 0) return;
        if (ip_len < 20) return;
        size_t ihl = (ip_pkt[0] & 0x0F) * 4;
        if (ihl < 20 || ip_len < ihl + 8) return;
        if (ip_pkt[9] != 17) return;  // только UDP

        const uint8_t* udp = ip_pkt + ihl;
        uint16_t sport = ((uint16_t)udp[0] << 8) | udp[1];
        uint16_t dport = ((uint16_t)udp[2] << 8) | udp[3];
        uint16_t ulen  = ((uint16_t)udp[4] << 8) | udp[5];
        if (ulen < 8 || ulen > ip_len - ihl) return;
        const uint8_t* payload = udp + 8;
        size_t plen = ulen - 8;

        struct sockaddr_in dst{};
        dst.sin_family = AF_INET;
        std::memcpy(&dst.sin_addr.s_addr, ip_pkt + 16, 4);
        dst.sin_port = htons(dport);

        ssize_t s = ::sendto(udp_bridge_sock, payload, plen, 0,
                             (struct sockaddr*)&dst, sizeof(dst));
        if (s > 0) {
            udp_bridge_tx++;
            if (sniff) std::printf("[ETH-BRIDGE] -> %s:%u %zd байт\n",
                                   inet_ntoa(dst.sin_addr), dport, s);
        }
#endif
    }

    // Приём из реального мира и упаковка обратно в Ethernet-кадр
    void udp_bridge_poll() {
#ifdef VIRTETH_HAVE_SOCK
        if (!udp_bridge_on || udp_bridge_sock < 0) return;
        for (int i = 0; i < 64; i++) {
            uint8_t buf[2048];
            struct sockaddr_in from{};
            socklen_t fl = sizeof(from);
            ssize_t r = ::recvfrom(udp_bridge_sock, buf, sizeof(buf), 0,
                                   (struct sockaddr*)&from, &fl);
            if (r <= 0) break;
            udp_bridge_rx++;

            // Собираем Ethernet + IPv4 + UDP + payload
            size_t plen = (size_t)r;
            size_t ulen = 8 + plen;
            size_t ilen = 20 + ulen;
            size_t total = 14 + ilen;
            std::vector<uint8_t> fr(total, 0);

            // Ethernet: dst = наш MAC, src = gw MAC
            std::memcpy(&fr[0], mac, 6);
            std::memcpy(&fr[6], gw_mac, 6);
            fr[12] = 0x08; fr[13] = 0x00;

            // IPv4
            uint8_t* ip = &fr[14];
            ip[0] = 0x45;
            ip[1] = 0;
            ip[2] = (uint8_t)(ilen >> 8); ip[3] = (uint8_t)(ilen & 0xFF);
            ip[4] = 0; ip[5] = 0;
            ip[6] = 0; ip[7] = 0;
            ip[8] = 64;
            ip[9] = 17; // UDP
            ip[10] = 0; ip[11] = 0;
            // src = отправитель (реальный хост), dst = шлюз (мы)
            std::memcpy(ip + 12, &from.sin_addr.s_addr, 4);
            std::memcpy(ip + 16, gw_ip, 4);
            uint16_t ic = ip_csum(ip, 20);
            ip[10] = (uint8_t)(ic >> 8); ip[11] = (uint8_t)(ic & 0xFF);

            // UDP
            uint8_t* udp = ip + 20;
            udp[0] = (uint8_t)(ntohs(from.sin_port) >> 8);
            udp[1] = (uint8_t)(ntohs(from.sin_port) & 0xFF);
            // dst port — не знаем без таблицы, подставим 0 (в sniff достаточно)
            udp[2] = 0; udp[3] = 0;
            udp[4] = (uint8_t)(ulen >> 8); udp[5] = (uint8_t)(ulen & 0xFF);
            udp[6] = 0; udp[7] = 0;
            std::memcpy(udp + 8, buf, plen);

            if (sniff) std::printf("[ETH-BRIDGE] <- %s:%u %zu байт\n",
                                   inet_ntoa(from.sin_addr), ntohs(from.sin_port), plen);

            if (rx_queue.size() < QMAX) {
                rx_queue.push_back(Frame{ std::move(fr) });
                stat_rx_ok++;
                stat_rx_bytes += (uint32_t)total;
                irq_stat |= 0x01;
            }
        }
#endif
    }

    // ---------- Виртуальный шлюз ----------'''
if old in src and 'udp_bridge_send' not in src:
    src = src.replace(old, new, 1); print("[+] методы udp_bridge")
elif 'udp_bridge_send' in src:
    print("[=] методы уже есть")
else:
    print("[!] не найден anchor gateway_handle"); ok = False

# 4) в transmit_frame вызвать udp_bridge_send
old = '''        if (loopback) { receive_frame(p, n); return; }
        gateway_handle(p, n);
'''
new = '''        if (loopback) { receive_frame(p, n); return; }
        gateway_handle(p, n);
        // IPv4/UDP → в реальный UDP-сокет
        if (n >= 14 && p[12] == 0x08 && p[13] == 0x00 && (p[14] >> 4) == 4) {
            udp_bridge_send(p + 14, n - 14);
        }
'''
if old in src and 'udp_bridge_send(p + 14' not in src:
    src = src.replace(old, new, 1); print("[+] вызов udp_bridge_send")
elif 'udp_bridge_send(p + 14' in src:
    print("[=] вызов уже есть")
else:
    print("[!] не найден transmit_frame anchor"); ok = False

# 5) в tick() — polling
old = '''    void tick() {}
'''
new = '''    void tick() { udp_bridge_poll(); }
'''
if old in src and 'udp_bridge_poll();' not in src:
    src = src.replace(old, new, 1); print("[+] tick -> udp_bridge_poll")
elif 'udp_bridge_poll();' in src:
    print("[=] tick уже пропатчен")
else:
    print("[!] не найден tick()"); ok = False

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
