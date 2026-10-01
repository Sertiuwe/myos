// virtnet.h — виртуальное сетевое устройство для ARM4 эмулятора
// MMIO: base 0x10015000, размер окна 0x1000
#ifndef VIRTNET_H
#define VIRTNET_H

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__ANDROID__)
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <errno.h>
  #define VIRTNET_HAVE_SOCKETS 1
#endif

class VirtNet {
public:
    static constexpr uint32_t R_TX_DATA    = 0x00;
    static constexpr uint32_t R_RX_DATA    = 0x04;
    static constexpr uint32_t R_STATUS     = 0x08;
    static constexpr uint32_t R_CTRL       = 0x0C;
    static constexpr uint32_t R_TX_LEN     = 0x10;
    static constexpr uint32_t R_RX_LEN     = 0x14;
    static constexpr uint32_t R_MAC_LO     = 0x18;
    static constexpr uint32_t R_MAC_HI     = 0x1C;
    static constexpr uint32_t R_IRQ_STAT   = 0x20;
    static constexpr uint32_t R_IRQ_MASK   = 0x24;
    static constexpr uint32_t R_IRQ_CLR    = 0x28;
    static constexpr uint32_t R_PEER_IP    = 0x2C;
    static constexpr uint32_t R_LOCAL_PORT = 0x30;
    static constexpr uint32_t R_PEER_PORT  = 0x34;

    uint32_t base;
    std::string name;

    uint32_t ctrl = 0;
    uint32_t irq_stat = 0;
    uint32_t irq_mask = 0;
    uint32_t tx_len = 0;
    uint32_t rx_len = 0;
    uint32_t mac_lo = 0x33221100u;
    uint32_t mac_hi = 0x00005544u;
    uint32_t peer_ip = 0;
    uint32_t local_port = 12345;
    uint32_t peer_port  = 12346;

    int  sock = -1;
    bool link_up  = false;
    bool loopback = false;

    static constexpr int RXBUF = 4096;
    uint8_t rx_buf[RXBUF];
    int rx_head = 0, rx_tail = 0;

    VirtNet(uint32_t b, const char* n) : base(b), name(n) {}
    ~VirtNet() { close_socket(); }

    bool contains(uint32_t a) const { return a >= base && a < base + 0x1000; }

    bool open_socket() {
#ifdef VIRTNET_HAVE_SOCKETS
        if (sock >= 0) return true;
        sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) { std::perror("[NET] socket"); return false; }

        int one = 1;
        ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons((uint16_t)local_port);
        if (::bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::perror("[NET] bind");
            ::close(sock); sock = -1; return false;
        }
        int flags = ::fcntl(sock, F_GETFL, 0);
        ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        link_up = true;
        std::printf("[NET] UDP открыт: 0.0.0.0:%u -> peer:%u\n",
                    local_port, peer_port);
        return true;
#else
        std::printf("[NET] сокеты не поддерживаются\n");
        return false;
#endif
    }

    void close_socket() {
#ifdef VIRTNET_HAVE_SOCKETS
        if (sock >= 0) { ::close(sock); sock = -1; }
        link_up = false;
#endif
    }

    void send_packet(const uint8_t* p, size_t n) {
#ifdef VIRTNET_HAVE_SOCKETS
        if (sock < 0) { std::printf("[NET] сокет не открыт\n"); return; }
        struct sockaddr_in peer{};
        peer.sin_family = AF_INET;
        peer.sin_port   = htons((uint16_t)peer_port);
        peer.sin_addr.s_addr = (peer_ip == 0)
            ? htonl(INADDR_LOOPBACK) : peer_ip;
        ssize_t sent = ::sendto(sock, p, n, 0,
                                (struct sockaddr*)&peer, sizeof(peer));
        if (sent < 0) std::perror("[NET] sendto");
        else std::printf("[NET] отправил %zd байт -> %s:%u\n",
                         sent, inet_ntoa(peer.sin_addr), peer_port);
#endif
    }

    void poll_recv() {
#ifdef VIRTNET_HAVE_SOCKETS
        if (sock < 0) return;
        for (;;) {
            uint8_t buf[1500];
            struct sockaddr_in from{};
            socklen_t fl = sizeof(from);
            ssize_t r = ::recvfrom(sock, buf, sizeof(buf), 0,
                                   (struct sockaddr*)&from, &fl);
            if (r <= 0) break;
            std::printf("[NET] принял %zd байт от %s:%u\n",
                        r, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            for (ssize_t i = 0; i < r; i++) push_rx(buf[i]);
            irq_stat |= 0x01;
            rx_len   += (uint32_t)r;
        }
#endif
    }

    void push_rx(uint8_t b) {
        int next = (rx_head + 1) % RXBUF;
        if (next == rx_tail) return;
        rx_buf[rx_head] = b;
        rx_head = next;
    }

    bool    rx_has() const { return rx_head != rx_tail; }
    uint8_t pop_rx() {
        if (rx_head == rx_tail) return 0;
        uint8_t v = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) % RXBUF;
        if (rx_head == rx_tail) irq_stat &= ~0x01u;
        return v;
    }

    uint32_t status() const {
        uint32_t s = 0x01;
        if (rx_has())  s |= 0x02;
        if (link_up)   s |= 0x04;
        return s;
    }

    uint32_t read(uint32_t addr, int width) {
        uint32_t off = addr - base;
        uint32_t v = 0;
        switch (off & ~3u) {
            case R_TX_DATA:    v = 0; break;
            case R_RX_DATA:    v = rx_has() ? pop_rx() : 0; break;
            case R_STATUS:     v = status(); break;
            case R_CTRL:       v = ctrl; break;
            case R_TX_LEN:     v = tx_len; break;
            case R_RX_LEN:     v = rx_len; break;
            case R_MAC_LO:     v = mac_lo; break;
            case R_MAC_HI:     v = mac_hi; break;
            case R_IRQ_STAT:   v = irq_stat & irq_mask; break;
            case R_IRQ_MASK:   v = irq_mask; break;
            case R_PEER_IP:    v = peer_ip; break;
            case R_LOCAL_PORT: v = local_port; break;
            case R_PEER_PORT:  v = peer_port; break;
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
            case R_TX_DATA: {
                if (!(ctrl & 0x01)) return;
                uint8_t b = (uint8_t)(v & 0xFF);
                if (loopback) { push_rx(b); irq_stat |= 0x01; }
                else {
                    tx_len++;
                    send_packet(&b, 1);
                }
                break;
            }
            case R_CTRL: {
                ctrl = v;
                loopback = (v & 0x02) != 0;
                if ((v & 0x01) && sock < 0) open_socket();
                if (!(v & 0x01) && sock >= 0) close_socket();
                break;
            }
            case R_IRQ_MASK: irq_mask = v; break;
            case R_IRQ_CLR:  irq_stat &= ~v; break;
            case R_PEER_IP:  peer_ip = v; break;
            case R_LOCAL_PORT: {
                uint32_t np = v & 0xFFFF;
                if (np != local_port) {
                    local_port = np;
                    close_socket();
                    if (ctrl & 0x01) open_socket();
                }
                break;
            }
            case R_PEER_PORT: peer_port = v & 0xFFFF; break;
            default: break;
        }
    }

    bool irq_pending() const { return (irq_stat & irq_mask) != 0; }
    void tick() { poll_recv(); }

    void dump() const {
        std::printf("--- VirtNet @ 0x%08X (%s) ---\n", base, name.c_str());
        std::printf("  CTRL=0x%X  STATUS=0x%X  link=%d  loopback=%d\n",
                    ctrl, status(), (int)link_up, (int)loopback);
        std::printf("  MAC=%08X:%08X  local=%u  peer_ip=0x%08X  peer=%u\n",
                    mac_hi, mac_lo, local_port, peer_ip, peer_port);
        std::printf("  IRQ stat=0x%X mask=0x%X  TX=%u RX=%u\n",
                    irq_stat, irq_mask, tx_len, rx_len);
    }
};

#endif // VIRTNET_H
