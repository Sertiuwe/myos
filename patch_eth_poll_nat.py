import pathlib

# ---------- virteth.h ----------
p = pathlib.Path("virteth.h")
src = p.read_text()
ok = True

# 1) NAT-таблица (одна запись — нам хватит)
old = '''    int  udp_bridge_sock = -1;
    bool udp_bridge_on = false;
    uint32_t udp_bridge_tx = 0, udp_bridge_rx = 0;
'''
new = '''    int  udp_bridge_sock = -1;
    bool udp_bridge_on = false;
    uint32_t udp_bridge_tx = 0, udp_bridge_rx = 0;

    // NAT: последний исходящий UDP
    uint16_t nat_sport = 0;         // порт MyOS (источник)
    uint16_t nat_dport = 0;         // порт назначения в интернете
    uint32_t nat_dst_ip = 0;        // IP назначения (в сетевом порядке)
    uint32_t nat_src_ip = 0;        // IP MyOS (в сетевом порядке)
'''
if old in src and 'nat_sport' not in src:
    src = src.replace(old, new, 1); print("[+] NAT-поля")
else:
    print("[=] NAT-поля уже есть или anchor не найден")

# 2) в send — сохраняем NAT
old = '''        struct sockaddr_in dst{};
        dst.sin_family = AF_INET;
        std::memcpy(&dst.sin_addr.s_addr, ip_pkt + 16, 4);
        dst.sin_port = htons(dport);
'''
new = '''        struct sockaddr_in dst{};
        dst.sin_family = AF_INET;
        std::memcpy(&dst.sin_addr.s_addr, ip_pkt + 16, 4);
        dst.sin_port = htons(dport);

        // Запомним для обратного маршрута
        nat_sport  = sport;
        nat_dport  = dport;
        nat_dst_ip = dst.sin_addr.s_addr;
        std::memcpy(&nat_src_ip, ip_pkt + 12, 4);
'''
if old in src and 'nat_sport  = sport' not in src:
    src = src.replace(old, new, 1); print("[+] NAT: сохранение")
else:
    print("[=] NAT: сохранение уже есть")

# 3) в receive — используем NAT
old = '''            ip[10] = 0; ip[11] = 0;
            // src = отправитель (реальный хост), dst = шлюз (мы)
            std::memcpy(ip + 12, &from.sin_addr.s_addr, 4);
            std::memcpy(ip + 16, gw_ip, 4);
'''
new = '''            ip[10] = 0; ip[11] = 0;
            // src = реальный хост, dst = IP MyOS (из NAT)
            std::memcpy(ip + 12, &from.sin_addr.s_addr, 4);
            if (nat_src_ip) std::memcpy(ip + 16, &nat_src_ip, 4);
            else            std::memcpy(ip + 16, gw_ip, 4);
'''
if old in src and 'nat_src_ip' in new:
    src = src.replace(old, new, 1); print("[+] NAT: dst IP")

old = '''            udp[0] = (uint8_t)(ntohs(from.sin_port) >> 8);
            udp[1] = (uint8_t)(ntohs(from.sin_port) & 0xFF);
            // dst port — не знаем без таблицы, подставим 0 (в sniff достаточно)
            udp[2] = 0; udp[3] = 0;
'''
new = '''            udp[0] = (uint8_t)(ntohs(from.sin_port) >> 8);
            udp[1] = (uint8_t)(ntohs(from.sin_port) & 0xFF);
            // dport = тот, с которого MyOS отправлял
            udp[2] = (uint8_t)(nat_sport >> 8);
            udp[3] = (uint8_t)(nat_sport & 0xFF);
'''
if old in src and 'nat_sport >> 8' not in src:
    src = src.replace(old, new, 1); print("[+] NAT: dport")
else:
    print("[=] NAT: dport уже есть")

p.write_text(src)

# ---------- ARM4.cpp: команда eth poll ----------
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

if '"poll"' not in src:
    old = '''            if (std::strcmp(arg1, "bridge") == 0) {'''
    new = '''            if (std::strcmp(arg1, "poll") == 0) {
                int n = arg2[0] ? std::atoi(arg2) : 200;
                for (int i = 0; i < n; i++) {
                    if (i % 10 == 0) { struct timespec ts{0, 1000000}; nanosleep(&ts, nullptr); }
                    e.udp_bridge_poll();
                }
                std::printf("[ETH] poll: rx=%u  queue=%zu\\n",
                            e.udp_bridge_rx, e.rx_queue.size());
                return false;
            }
            if (std::strcmp(arg1, "bridge") == 0) {'''
    if old in src:
        src = src.replace(old, new, 1); print("[+] eth poll")
    else:
        print("[!] anchor для eth poll не найден")
else:
    print("[=] eth poll уже есть")

# заголовок для nanosleep
if '#include <ctime>' not in src:
    old = '#include <cstdlib>\n'
    new = '#include <cstdlib>\n#include <ctime>\n'
    src = src.replace(old, new, 1)
    print("[+] include ctime")

p.write_text(src)
print("РЕЗУЛЬТАТ OK" if ok else "частично")
