#include "net.h"
#include "eth.h"
#include "print.h"

#define yield() __asm__ volatile ("swi 0")
extern volatile uint32_t tick_count;

/* ============================================================
 *  Ethernet / IP / UDP / ARP / ICMP / DNS
 * ============================================================ */

#define ETHERTYPE_IP   0x0800
#define ETHERTYPE_ARP  0x0806

#define IP_PROTO_ICMP  1
#define IP_PROTO_UDP   17
#define IP_PROTO_TCP   6

#define ARP_REQ  1
#define ARP_REP  2

#define ICMP_ECHO_REQ 8
#define ICMP_ECHO_REP 0

#define ARP_TABLE_SIZE 8

typedef struct {
    uint8_t  ip[4];
    uint8_t  mac[6];
    int      valid;
    uint32_t age;
} arp_entry_t;

static uint8_t g_mymac[6] = {0x02,0x00,0x00,0x00,0x00,0x42};
static uint8_t g_myip[4]  = {192,168,0,2};
static uint8_t g_gwip[4]  = {192,168,0,1};
static uint8_t g_gwmac[6] = {0x02,0x00,0x00,0x00,0x00,0xFE};

static arp_entry_t g_arp[ARP_TABLE_SIZE];

static uint16_t g_ip_id = 0x1000;

/* ---------- утилиты ---------- */
static uint16_t htons16(uint16_t v) { return (uint16_t)((v<<8)|(v>>8)); }
static uint16_t ntohs16(uint16_t v) { return htons16(v); }

static uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static void wr16(uint8_t* p, uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void wr32(uint8_t* p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8);  p[3]=(uint8_t)v;
}

static uint16_t ip_checksum(const uint8_t* p, int len) {
    uint32_t s = 0;
    for (int i = 0; i + 1 < len; i += 2) s += ((uint32_t)p[i]<<8)|p[i+1];
    if (len & 1) s += ((uint32_t)p[len-1]<<8);
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (uint16_t)~s;
}

static int ip_eq(const uint8_t a[4], const uint8_t b[4]) {
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}

void net_print_ip(const uint8_t ip[4]) {
    putuint(ip[0]); putc('.');
    putuint(ip[1]); putc('.');
    putuint(ip[2]); putc('.');
    putuint(ip[3]);
}

/* ---------- ARP таблица ---------- */
static void arp_add(const uint8_t ip[4], const uint8_t mac[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp[i].valid && ip_eq(g_arp[i].ip, ip)) {
            for (int k = 0; k < 6; k++) g_arp[i].mac[k] = mac[k];
            g_arp[i].age = 0;
            return;
        }
    }
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!g_arp[i].valid) {
            for (int k = 0; k < 4; k++) g_arp[i].ip[k]  = ip[k];
            for (int k = 0; k < 6; k++) g_arp[i].mac[k] = mac[k];
            g_arp[i].valid = 1;
            g_arp[i].age = 0;
            return;
        }
    }
    // выкинем самую старую
    int oldest = 0;
    for (int i = 1; i < ARP_TABLE_SIZE; i++)
        if (g_arp[i].age > g_arp[oldest].age) oldest = i;
    for (int k = 0; k < 4; k++) g_arp[oldest].ip[k]  = ip[k];
    for (int k = 0; k < 6; k++) g_arp[oldest].mac[k] = mac[k];
    g_arp[oldest].valid = 1;
    g_arp[oldest].age = 0;
}

static int arp_lookup(const uint8_t ip[4], uint8_t out_mac[6]) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (g_arp[i].valid && ip_eq(g_arp[i].ip, ip)) {
            for (int k = 0; k < 6; k++) out_mac[k] = g_arp[i].mac[k];
            return 1;
        }
    }
    return 0;
}

void net_show_arp(void) {
    puts("ARP table:\n");
    int any = 0;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!g_arp[i].valid) continue;
        any = 1;
        puts("  ");
        net_print_ip(g_arp[i].ip);
        puts("  -> ");
        for (int k = 0; k < 6; k++) {
            puthex8(g_arp[i].mac[k]);
            if (k < 5) putc(':');
        }
        puts("\n");
    }
    if (!any) puts("  (empty)\n");
}

/* ---------- отправка ARP ---------- */
static void send_arp_req(const uint8_t target_ip[4]) {
    uint8_t fr[42];
    for (int i = 0; i < 6; i++) fr[i]     = 0xFF;           // broadcast
    for (int i = 0; i < 6; i++) fr[6+i]   = g_mymac[i];
    wr16(fr+12, ETHERTYPE_ARP);
    wr16(fr+14, 0x0001);  // htype
    wr16(fr+16, 0x0800);  // ptype
    fr[18] = 6;  fr[19] = 4;
    wr16(fr+20, ARP_REQ);
    for (int i = 0; i < 6; i++) fr[22+i] = g_mymac[i];
    for (int i = 0; i < 4; i++) fr[28+i] = g_myip[i];
    for (int i = 0; i < 6; i++) fr[32+i] = 0;
    for (int i = 0; i < 4; i++) fr[38+i] = target_ip[i];
    eth_send(fr, 42);
}

/* ---------- отправка IP-пакета (UDP/ICMP) ---------- */
static void ip_send(const uint8_t dst_mac[6], const uint8_t dst_ip[4],
                    uint8_t proto, const uint8_t* payload, uint32_t plen)
{
    static uint8_t frame[ETH_FRAME_MAX];
    uint32_t total = 14 + 20 + plen;
    if (total > ETH_FRAME_MAX) return;

    for (int i = 0; i < 6; i++) frame[i]   = dst_mac[i];
    for (int i = 0; i < 6; i++) frame[6+i] = g_mymac[i];
    wr16(frame+12, ETHERTYPE_IP);

    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    ip[1] = 0;
    wr16(ip+2, (uint16_t)(20 + plen));
    wr16(ip+4, g_ip_id++);
    wr16(ip+6, 0x4000);   // DF
    ip[8] = 64;
    ip[9] = proto;
    wr16(ip+10, 0);
    for (int i = 0; i < 4; i++) ip[12+i] = g_myip[i];
    for (int i = 0; i < 4; i++) ip[16+i] = dst_ip[i];
    uint16_t c = ip_checksum(ip, 20);
    wr16(ip+10, c);

    for (uint32_t i = 0; i < plen; i++) ip[20+i] = payload[i];

    eth_send(frame, total);
}

/* ---------- ARP resolve с ожиданием ---------- */
extern volatile uint32_t tick_count;

int net_arp_resolve(const uint8_t ip[4], uint8_t out_mac[6]) {
    if (arp_lookup(ip, out_mac)) return 0;
    if (ip_eq(ip, g_gwip)) {
        for (int i = 0; i < 6; i++) out_mac[i] = g_gwmac[i];
        arp_add(ip, g_gwmac);
        return 0;
    }
    send_arp_req(ip);
    uint32_t t0 = tick_count;
    while ((tick_count - t0) < 200) {   // 2 секунды
        net_poll();
        if (arp_lookup(ip, out_mac)) return 0;
        yield();
    }
    return -1;
}

/* ---------- ICMP echo ---------- */
int net_ping(const uint8_t ip[4], int count) {
    uint8_t dst_mac[6];
    puts("PING "); net_print_ip(ip); puts("\n");

    if (net_arp_resolve(ip, dst_mac) < 0) {
        puts("  arp resolve failed\n");
        return -1;
    }

    int ok = 0;
    for (int seq = 0; seq < count; seq++) {
        uint8_t pl[8 + 32];
        pl[0] = ICMP_ECHO_REQ;
        pl[1] = 0;
        wr16(pl+2, 0);
        wr16(pl+4, 0x1234);          // id
        wr16(pl+6, (uint16_t)seq);   // seq
        for (int i = 8; i < 8 + 32; i++) pl[i] = (uint8_t)i;

        // checksum по ICMP
        uint16_t c = ip_checksum(pl, 8 + 32);
        wr16(pl+2, c);

        uint32_t t0 = tick_count;
        ip_send(dst_mac, ip, IP_PROTO_ICMP, pl, 8 + 32);

        // ждём ответ
        int got = 0;
        while ((tick_count - t0) < 200) {
            net_poll();
            // ответ придёт в g_last_icmp — см. обработчик ниже
            extern volatile int g_icmp_reply_ready;
            extern volatile uint16_t g_icmp_reply_id, g_icmp_reply_seq;
            if (g_icmp_reply_ready) {
                g_icmp_reply_ready = 0;
                got = 1;
                break;
            }
            yield();
        }
        if (got) {
            puts("  reply from "); net_print_ip(ip);
            puts(": seq="); putuint((uint32_t)seq); puts("\n");
            ok++;
        } else {
            puts("  timeout seq="); putuint((uint32_t)seq); puts("\n");
        }
    }
    puts("  "); putuint((uint32_t)ok);
    puts("/"); putuint((uint32_t)count);
    puts(" replies\n");
    return ok;
}

/* ---------- DNS ---------- */
int net_dns_resolve(const char* hostname, uint8_t out_ip[4]) {
    uint8_t dst_mac[6];
    uint8_t dns_ip[4] = {8,8,8,8};

    if (net_arp_resolve(g_gwip, dst_mac) < 0) return -1;

    // Формируем DNS-запрос
    static uint8_t udp[512];
    // UDP-заголовок
    wr16(udp+0, 0x1234);       // sport
    wr16(udp+2, 53);           // dport
    wr16(udp+4, 0);            // длина — заполним после
    wr16(udp+6, 0);            // checksum=0

    // DNS payload
    uint8_t* dns = udp + 8;
    uint32_t dn = 0;
    wr16(dns+dn, 0xABCD); dn += 2;   // ID
    wr16(dns+dn, 0x0100); dn += 2;   // RD, no error
    wr16(dns+dn, 1);      dn += 2;   // QDCOUNT
    wr16(dns+dn, 0);      dn += 2;   // ANCOUNT
    wr16(dns+dn, 0);      dn += 2;   // NSCOUNT
    wr16(dns+dn, 0);      dn += 2;   // ARCOUNT

    // QNAME
    const char* p = hostname;
    while (*p) {
        const char* dot = p;
        int len = 0;
        while (*dot && *dot != '.' && len < 63) { dot++; len++; }
        dns[dn++] = (uint8_t)len;
        for (int i = 0; i < len; i++) dns[dn++] = (uint8_t)p[i];
        p = dot;
        if (*p == '.') p++;
    }
    dns[dn++] = 0;
    wr16(dns+dn, 1); dn += 2;        // QTYPE A
    wr16(dns+dn, 1); dn += 2;        // QCLASS IN

    uint32_t total_payload = 8 + dn;
    wr16(udp+4, (uint16_t)total_payload);

    puts("DNS: resolving "); puts(hostname); puts("...\n");
    ip_send(dst_mac, dns_ip, IP_PROTO_UDP, udp, total_payload);

    extern volatile int  g_udp_reply_ready;
    extern volatile uint8_t g_udp_reply[512];
    extern volatile int  g_udp_reply_len;
    g_udp_reply_ready = 0;

    uint32_t t0 = tick_count;
    while ((tick_count - t0) < 400) {   // 4 сек
        net_poll();
        if (g_udp_reply_ready) break;
        yield();
    }
    if (!g_udp_reply_ready) { puts("  timeout\n"); return -1; }

    // Разбор DNS-ответа
    const uint8_t* r = (const uint8_t*)g_udp_reply;
    int rl = g_udp_reply_len;
    if (rl < 12) { puts("  short reply\n"); return -1; }
    // пропускаем заголовок
    int off = 12;
    // пропускаем QNAME (эхо)
    while (off < rl && r[off] != 0) {
        int l = r[off];
        if ((l & 0xC0) == 0xC0) { off += 2; break; }
        off += 1 + l;
    }
    if (off < rl) off += 1;   // нулевой байт
    off += 4;                 // QTYPE + QCLASS

    int ancount = ((int)r[6] << 8) | r[7];
    for (int a = 0; a < ancount && off + 12 <= rl; a++) {
        // NAME
        if ((r[off] & 0xC0) == 0xC0) off += 2;
        else { while (off < rl && r[off]) off += 1 + r[off]; off++; }
        if (off + 10 > rl) break;
        uint16_t type = ((uint16_t)r[off]<<8) | r[off+1];
        uint16_t cls  = ((uint16_t)r[off+2]<<8) | r[off+3];
        uint32_t ttl  = rd32(r+off+4);
        uint16_t rdlen= ((uint16_t)r[off+8]<<8) | r[off+9];
        (void)ttl;
        off += 10;
        if (type == 1 && cls == 1 && rdlen == 4) {
            out_ip[0] = r[off];
            out_ip[1] = r[off+1];
            out_ip[2] = r[off+2];
            out_ip[3] = r[off+3];
            return 0;
        }
        off += rdlen;
    }
    puts("  no A record\n");
    return -1;
}

/* ---------- флаги для ожидания ---------- */
volatile int g_icmp_reply_ready = 0;
volatile uint16_t g_icmp_reply_id = 0, g_icmp_reply_seq = 0;
volatile int g_udp_reply_ready = 0;
volatile uint8_t g_udp_reply[512];
volatile int g_udp_reply_len = 0;

/* ---------- обработка принятого кадра ---------- */
void net_poll(void) {
    static uint8_t fr[ETH_FRAME_MAX];
    int n;
    while ((n = eth_recv(fr, sizeof(fr))) > 0) {
        if (n < 14) continue;
        uint16_t et = ((uint16_t)fr[12]<<8) | fr[13];

        if (et == ETHERTYPE_ARP && n >= 42) {
            uint16_t op = ((uint16_t)fr[20]<<8) | fr[21];
            uint8_t  spa[4], sha[6];
            for (int i = 0; i < 6; i++) sha[i] = fr[22+i];
            for (int i = 0; i < 4; i++) spa[i] = fr[28+i];
            arp_add(spa, sha);
            if (op == ARP_REQ) {
                // мы не отвечаем на ARP-запросы, только в шлюз (эмулятор)
            }
            continue;
        }

        if (et == ETHERTYPE_IP && n >= 34) {
            const uint8_t* ip = fr + 14;
            int ihl = (ip[0] & 0x0F) * 4;
            if (ihl < 20 || n < 14 + ihl) continue;
            uint8_t proto = ip[9];
            const uint8_t* payload = ip + ihl;
            int plen = n - 14 - ihl;

            if (proto == IP_PROTO_ICMP && plen >= 8) {
                uint8_t type = payload[0];
                if (type == ICMP_ECHO_REP) {
                    g_icmp_reply_id  = ((uint16_t)payload[4]<<8) | payload[5];
                    g_icmp_reply_seq = ((uint16_t)payload[6]<<8) | payload[7];
                    g_icmp_reply_ready = 1;
                }
                continue;
            }

            if (proto == IP_PROTO_UDP && plen >= 8) {
                uint16_t dport = ((uint16_t)payload[2]<<8) | payload[3];
                int ulen = plen - 8;
                if (ulen > 500) ulen = 500;
                if (dport == 0x1234) {
                    for (int i = 0; i < ulen; i++) g_udp_reply[i] = payload[8+i];
                    g_udp_reply_len = ulen;
                    g_udp_reply_ready = 1;
                }
                continue;
            }
        }
    }
}

void net_init(void) {
    eth_init(g_mymac);
    for (int i = 0; i < ARP_TABLE_SIZE; i++) g_arp[i].valid = 0;
    arp_add(g_gwip, g_gwmac);
}

void net_get_myip(uint8_t out[4]) { for (int i=0;i<4;i++) out[i]=g_myip[i]; }
void net_get_gwip(uint8_t out[4]) { for (int i=0;i<4;i++) out[i]=g_gwip[i]; }
void net_set_myip(const uint8_t ip[4]) { for (int i=0;i<4;i++) g_myip[i]=ip[i]; }
void net_set_gwip(const uint8_t ip[4]) { for (int i=0;i<4;i++) g_gwip[i]=ip[i]; }
