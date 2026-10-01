// virteth.h — виртуальный Ethernet-контроллер для ARM4
// MMIO: base 0x10016000, окно 0x1000
#ifndef VIRTETH_H
#define VIRTETH_H

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#if defined(__unix__) || defined(__ANDROID__)
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <errno.h>
  #define VIRTETH_HAVE_SOCK 1
#endif

class VirtEth {
public:
    // ---- Регистры ----
    static constexpr uint32_t R_MAC_LO        = 0x00; // R/W MAC[0..3]
    static constexpr uint32_t R_MAC_HI        = 0x04; // R/W MAC[4..5]
    static constexpr uint32_t R_CTRL          = 0x08;
    static constexpr uint32_t R_STATUS        = 0x0C;
    static constexpr uint32_t R_IRQ_STAT      = 0x10;
    static constexpr uint32_t R_IRQ_MASK      = 0x14;
    static constexpr uint32_t R_IRQ_CLR       = 0x18;
    static constexpr uint32_t R_TX_LEN        = 0x20;
    static constexpr uint32_t R_TX_PUSH       = 0x24; // W байт
    static constexpr uint32_t R_TX_COMMIT     = 0x28; // W 1 = отправить
    static constexpr uint32_t R_TX_ABORT      = 0x2C; // W 1 = сбросить буфер
    static constexpr uint32_t R_RX_LEN        = 0x30; // R длина текущего кадра
    static constexpr uint32_t R_RX_POP        = 0x34; // R байт
    static constexpr uint32_t R_RX_NEXT       = 0x38; // W 1 = выкинуть текущий
    static constexpr uint32_t R_RX_COUNT      = 0x3C; // R кадров в очереди
    static constexpr uint32_t R_STATS_TX_OK   = 0x40;
    static constexpr uint32_t R_STATS_RX_OK   = 0x44;
    static constexpr uint32_t R_STATS_TX_ERR  = 0x48;
    static constexpr uint32_t R_STATS_RX_ERR  = 0x4C;
    static constexpr uint32_t R_STATS_TX_BYTE = 0x50;
    static constexpr uint32_t R_STATS_RX_BYTE = 0x54;
    static constexpr uint32_t R_FILTER_LO     = 0x60;
    static constexpr uint32_t R_FILTER_HI     = 0x64;

    // ---- CTRL биты ----
    static constexpr uint32_t C_ENABLE       = 0x01;
    static constexpr uint32_t C_PROMISC      = 0x02;
    static constexpr uint32_t C_LOOPBACK     = 0x04;
    static constexpr uint32_t C_IRQ_EN       = 0x08;
    static constexpr uint32_t C_ACCEPT_BCAST = 0x10;
    static constexpr uint32_t C_ACCEPT_MCAST = 0x20;
    static constexpr uint32_t C_ACCEPT_UNI   = 0x40;

    // ---- STATUS биты ----
    static constexpr uint32_t S_LINK    = 0x01;
    static constexpr uint32_t S_TX_RDY  = 0x02;
    static constexpr uint32_t S_RX_AVAIL= 0x04;
    static constexpr uint32_t S_TX_OVF  = 0x08;
    static constexpr uint32_t S_RX_OVF  = 0x10;

    static constexpr size_t MTU   = 1518;
    static constexpr size_t QMAX  = 32;

    uint32_t    base;
    std::string name;

    // MAC
    uint8_t mac[6] = {0x02,0x00,0x00,0x00,0x00,0x01};
    uint8_t filter[6] = {0,0,0,0,0,0};

    // CTRL
    bool enable = false, promisc = false, loopback = false;
    bool irq_en = false, accept_bcast = true, accept_mcast = true, accept_uni = true;

    // IRQ
    uint32_t irq_stat = 0;
    uint32_t irq_mask = 0;

    // TX
    std::vector<uint8_t> tx_buf;
    uint32_t tx_commit_count = 0;
    bool     tx_ovf = false;

    // RX
    struct Frame { std::vector<uint8_t> data; };
    std::deque<Frame> rx_queue;
    std::vector<uint8_t> rx_current;
    size_t   rx_pos = 0;
    bool     rx_ovf = false;

    // Stats
    uint32_t stat_tx_ok = 0, stat_rx_ok = 0;
    uint32_t stat_tx_err = 0, stat_rx_err = 0;
    uint32_t stat_tx_bytes = 0, stat_rx_bytes = 0;

    // Отладка
    bool sniff = false;

    // UDP-bridge (NAT в реальный UDP)
    int  udp_bridge_sock = -1;
    bool udp_bridge_on = false;
    uint32_t udp_bridge_tx = 0, udp_bridge_rx = 0;

    // NAT: последний исходящий UDP
    uint16_t nat_sport = 0;         // порт MyOS (источник)
    uint16_t nat_dport = 0;         // порт назначения в интернете
    uint32_t nat_dst_ip = 0;        // IP назначения (в сетевом порядке)
    uint32_t nat_src_ip = 0;        // IP MyOS (в сетевом порядке)

    // Виртуальный шлюз (ARP + ICMP responder)
    bool    gateway_enabled = true;
    uint8_t gw_mac[6] = {0x02,0x00,0x00,0x00,0x00,0xFE};
    uint8_t gw_ip[4]  = {10,0,0,1};

    VirtEth(uint32_t b, const char* n) : base(b), name(n) {}

    bool contains(uint32_t a) const { return a >= base && a < base + 0x1000; }

    uint32_t status() const {
        uint32_t s = S_LINK | S_TX_RDY;
        if (rx_has()) s |= S_RX_AVAIL;
        if (tx_ovf)   s |= S_TX_OVF;
        if (rx_ovf)   s |= S_RX_OVF;
        return s;
    }

    bool rx_has() const { return !rx_current.empty() || !rx_queue.empty(); }

    void set_mac_lo(uint32_t v) {
        mac[0]=(v>>0)&0xFF; mac[1]=(v>>8)&0xFF;
        mac[2]=(v>>16)&0xFF; mac[3]=(v>>24)&0xFF;
    }
    void set_mac_hi(uint32_t v) {
        mac[4]=(v>>0)&0xFF; mac[5]=(v>>8)&0xFF;
    }
    uint32_t get_mac_lo() const {
        return (uint32_t)mac[0] | ((uint32_t)mac[1]<<8) |
               ((uint32_t)mac[2]<<16) | ((uint32_t)mac[3]<<24);
    }
    uint32_t get_mac_hi() const { return (uint32_t)mac[4] | ((uint32_t)mac[5]<<8); }

    bool frame_matches(const uint8_t* p, size_t n) const {
        if (n < 14) return false;
        bool bcast = true, mcast = (p[0] & 0x01) != 0;
        for (int i = 0; i < 6; i++) if (p[i] != 0xFF) { bcast = false; break; }
        if (promisc) return true;
        if (bcast && accept_bcast) return true;
        if (mcast && !bcast && accept_mcast) return true;
        if (accept_uni) {
            bool m = true;
            for (int i = 0; i < 6; i++) if (p[i] != mac[i]) { m = false; break; }
            if (m) return true;
            bool fm = true;
            for (int i = 0; i < 6; i++) if (p[i] != filter[i]) { fm = false; break; }
            if (fm && (filter[0]|filter[1]|filter[2]|filter[3]|filter[4]|filter[5])) return true;
        }
        return false;
    }

    void sniff_dump(const char* dir, const uint8_t* p, size_t n) {
        if (!sniff) return;
        std::printf("[ETH %s %s] %zu байт: ", name.c_str(), dir, n);
        for (size_t i = 0; i < n && i < 32; i++) std::printf("%02X", p[i]);
        if (n > 32) std::printf("...");
        std::printf("\n");
    }

    // Принять кадр (из loopback или из внешнего мира)
    void receive_frame(const uint8_t* p, size_t n) {
        if (!enable) return;
        if (n < 14 || n > MTU) { stat_rx_err++; return; }
        if (!frame_matches(p, n)) return;
        sniff_dump("RX", p, n);
        if (rx_queue.size() >= QMAX) { rx_ovf = true; stat_rx_err++; return; }
        rx_queue.push_back(Frame{ std::vector<uint8_t>(p, p + n) });
        stat_rx_ok++;
        stat_rx_bytes += (uint32_t)n;
        irq_stat |= 0x01;
        rx_ovf = false;
    }

    // Отправить кадр (в loopback или наружу)
    void transmit_frame(const uint8_t* p, size_t n) {
        if (!enable) { stat_tx_err++; return; }
        if (n < 14 || n > MTU) { stat_tx_err++; return; }
        sniff_dump("TX", p, n);
        stat_tx_ok++;
        stat_tx_bytes += (uint32_t)n;
        if (loopback) { receive_frame(p, n); return; }
        gateway_handle(p, n);
        // IPv4/UDP → в реальный UDP-сокет
        if (n >= 14 && p[12] == 0x08 && p[13] == 0x00 && (p[14] >> 4) == 4) {
            udp_bridge_send(p + 14, n - 14);
        }
    }

    // ---------- UDP-мост в реальную сеть ----------
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
        if (ulen < 8 || ulen > ip_len - ihl) {
            if (sniff) std::printf("[ETH-BRIDGE] fix ulen: %u -> %zu\n",
                                   ulen, ip_len - ihl);
            ulen = (uint16_t)(ip_len - ihl);
            if (ulen < 8) return;
        }
        const uint8_t* payload = udp + 8;
        size_t plen = ulen - 8;

        struct sockaddr_in dst{};
        dst.sin_family = AF_INET;
        std::memcpy(&dst.sin_addr.s_addr, ip_pkt + 16, 4);
        dst.sin_port = htons(dport);

        // Запомним для обратного маршрута
        nat_sport  = sport;
        nat_dport  = dport;
        nat_dst_ip = dst.sin_addr.s_addr;
        std::memcpy(&nat_src_ip, ip_pkt + 12, 4);

        if (sniff) std::printf("[ETH-BRIDGE] try -> %s:%u, %zu байт\n",
                               inet_ntoa(dst.sin_addr), dport, plen);
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
            // src = реальный хост, dst = IP MyOS (из NAT)
            std::memcpy(ip + 12, &from.sin_addr.s_addr, 4);
            if (nat_src_ip) std::memcpy(ip + 16, &nat_src_ip, 4);
            else            std::memcpy(ip + 16, gw_ip, 4);
            uint16_t ic = ip_csum(ip, 20);
            ip[10] = (uint8_t)(ic >> 8); ip[11] = (uint8_t)(ic & 0xFF);

            // UDP
            uint8_t* udp = ip + 20;
            udp[0] = (uint8_t)(ntohs(from.sin_port) >> 8);
            udp[1] = (uint8_t)(ntohs(from.sin_port) & 0xFF);
            // dport = тот, с которого MyOS отправлял
            udp[2] = (uint8_t)(nat_sport >> 8);
            udp[3] = (uint8_t)(nat_sport & 0xFF);
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

    // ---------- Виртуальный шлюз ----------
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

    uint32_t read(uint32_t addr, int width) {
        uint32_t off = addr - base;
        uint32_t v = 0;
        switch (off & ~3u) {
            case R_MAC_LO:     v = get_mac_lo(); break;
            case R_MAC_HI:     v = get_mac_hi(); break;
            case R_CTRL:       v = (enable?C_ENABLE:0)|(promisc?C_PROMISC:0)|
                                  (loopback?C_LOOPBACK:0)|(irq_en?C_IRQ_EN:0)|
                                  (accept_bcast?C_ACCEPT_BCAST:0)|
                                  (accept_mcast?C_ACCEPT_MCAST:0)|
                                  (accept_uni?C_ACCEPT_UNI:0); break;
            case R_STATUS:     v = status(); break;
            case R_IRQ_STAT:   v = irq_stat; break;
            case R_IRQ_MASK:   v = irq_mask; break;
            case R_TX_LEN:     v = (uint32_t)tx_buf.size(); break;
            case R_TX_COMMIT:  v = tx_commit_count; break;
            case R_RX_LEN:     v = (uint32_t)rx_current.size(); break;
            case R_RX_COUNT:   v = (uint32_t)(rx_queue.size() + (rx_current.empty()?0:1)); break;
            case R_RX_POP:
                if (rx_current.empty()) {
                    if (!rx_queue.empty()) { rx_current = rx_queue.front().data; rx_queue.pop_front(); rx_pos = 0; }
                }
                if (rx_pos < rx_current.size()) v = rx_current[rx_pos++];
                else v = 0;
                break;
            case R_STATS_TX_OK:    v = stat_tx_ok; break;
            case R_STATS_RX_OK:    v = stat_rx_ok; break;
            case R_STATS_TX_ERR:   v = stat_tx_err; break;
            case R_STATS_RX_ERR:   v = stat_rx_err; break;
            case R_STATS_TX_BYTE:  v = stat_tx_bytes; break;
            case R_STATS_RX_BYTE:  v = stat_rx_bytes; break;
            case R_FILTER_LO:      v = (uint32_t)filter[0]|((uint32_t)filter[1]<<8)|
                                       ((uint32_t)filter[2]<<16)|((uint32_t)filter[3]<<24); break;
            case R_FILTER_HI:      v = (uint32_t)filter[4]|((uint32_t)filter[5]<<8); break;
            default: v = 0;
        }
        if (width == 2) v = (v >> ((addr & 2) * 8)) & 0xFFFF;
        else if (width == 1) v = (v >> ((addr & 3) * 8)) & 0xFF;
        return v;
    }

    void write(uint32_t addr, uint32_t v, int width) {
        (void)width;
        uint32_t off = addr - base;
        switch (off & ~3u) {
            case R_MAC_LO:    set_mac_lo(v); break;
            case R_MAC_HI:    set_mac_hi(v); break;
            case R_CTRL:
                enable       = (v & C_ENABLE) != 0;
                promisc      = (v & C_PROMISC) != 0;
                loopback     = (v & C_LOOPBACK) != 0;
                irq_en       = (v & C_IRQ_EN) != 0;
                accept_bcast = (v & C_ACCEPT_BCAST) != 0;
                accept_mcast = (v & C_ACCEPT_MCAST) != 0;
                accept_uni   = (v & C_ACCEPT_UNI) != 0;
                break;
            case R_IRQ_MASK:  irq_mask = v; break;
            case R_IRQ_CLR:   irq_stat &= ~v; break;
            case R_TX_LEN:    tx_buf.clear(); tx_buf.reserve(v & 0xFFFF); tx_ovf = false; break;
            case R_TX_PUSH:
                if (tx_buf.size() < MTU) tx_buf.push_back((uint8_t)(v & 0xFF));
                else tx_ovf = true;
                break;
            case R_TX_COMMIT:
                if (v & 1) {
                    if (tx_buf.size() >= 14) transmit_frame(tx_buf.data(), tx_buf.size());
                    else stat_tx_err++;
                    tx_buf.clear();
                    tx_commit_count++;
                }
                break;
            case R_TX_ABORT:  tx_buf.clear(); break;
            case R_RX_NEXT:
                if (v & 1) {
                    if (!rx_current.empty()) rx_current.clear();
                    if (!rx_queue.empty()) {
                        rx_current = rx_queue.front().data;
                        rx_queue.pop_front();
                    }
                    rx_pos = 0;
                }
                break;
            case R_FILTER_LO:
                filter[0]=(v>>0)&0xFF; filter[1]=(v>>8)&0xFF;
                filter[2]=(v>>16)&0xFF; filter[3]=(v>>24)&0xFF; break;
            case R_FILTER_HI:
                filter[4]=(v>>0)&0xFF; filter[5]=(v>>8)&0xFF; break;
            default: break;
        }
    }

    bool irq_pending() const { return irq_en && (irq_stat & (irq_mask ? irq_mask : 1u)); }
    void tick() { udp_bridge_poll(); }

    void mac_str(char* out) const {
        std::sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    }

    void dump() const {
        char m[32]; mac_str(m);
        std::printf("--- VirtEth @ 0x%08X (%s) ---\n", base, name.c_str());
        std::printf("  MAC=%s  enable=%d promisc=%d loopback=%d irq_en=%d\n",
                    m, (int)enable, (int)promisc, (int)loopback, (int)irq_en);
        std::printf("  CTRL: bcast=%d mcast=%d uni=%d  STATUS=0x%02X\n",
                    (int)accept_bcast,(int)accept_mcast,(int)accept_uni, status());
        std::printf("  IRQ: stat=0x%X mask=0x%X\n", irq_stat, irq_mask);
        std::printf("  TX: buf=%zu commits=%u ok=%u err=%u bytes=%u\n",
                    tx_buf.size(), tx_commit_count, stat_tx_ok, stat_tx_err, stat_tx_bytes);
        std::printf("  RX: queue=%zu cur=%zu/%zu ok=%u err=%u bytes=%u\n",
                    rx_queue.size(), rx_pos, rx_current.size(),
                    stat_rx_ok, stat_rx_err, stat_rx_bytes);
    }
};

#endif // VIRTETH_H
