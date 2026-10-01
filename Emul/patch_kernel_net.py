import pathlib

p = pathlib.Path("kernel.c")
src = p.read_text()
ok = True

def sub_once(s, old, new, tag):
    global ok
    if old not in s:
        print(f"[!] нет: {tag}"); ok = False; return s
    if s.count(old) != 1:
        print(f"[!] неоднозначно ({s.count(old)}): {tag}"); ok = False; return s
    print(f"[+] {tag}")
    return s.replace(old, new, 1)

# 1) includes
if '#include "net.h"' not in src:
    src = sub_once(src, '#include "symbols.h"\n',
                   '#include "symbols.h"\n#include "eth.h"\n#include "net.h"\n',
                   'includes eth/net')

# 2) IRQ 15 в vic_init
if '(1u << 15)' not in src:
    src = sub_once(src,
        '    VIC_INTENABLE = (1u << 4) | (1u << 12);\n',
        '    VIC_INTENABLE = (1u << 4) | (1u << 12) | (1u << 15);\n',
        'VIC: включить IRQ 15')

# 3) IRQ 15 в irq_dispatch
if 'eth_irq_handler();' not in src:
    old = '''    if (status & (1u << 12)) {
        uint32_t c = UART_DR;
        rx_push((char)(c & 0xFF));
        UART_ICR = 0x10;
    }
'''
    new = '''    if (status & (1u << 12)) {
        uint32_t c = UART_DR;
        rx_push((char)(c & 0xFF));
        UART_ICR = 0x10;
    }
    if (status & (1u << 15)) {
        eth_irq_handler();
    }
'''
    src = sub_once(src, old, new, 'IRQ 15 handler')

# 4) parse_ip helper + команды
if 'static int parse_ip(' not in src:
    old = '''static int parse_dec(const char *s, uint32_t *out) {'''
    new = '''static int parse_ip(const char *s, uint8_t out[4]) {
    int n = 0;
    while (*s && n < 4) {
        uint32_t v = 0;
        int any = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (uint32_t)(*s - '0');
            s++; any = 1;
        }
        if (!any || v > 255) return 0;
        out[n++] = (uint8_t)v;
        if (*s == '.') s++;
        else break;
    }
    return (n == 4 && *s == 0);
}

static int parse_dec(const char *s, uint32_t *out) {'''
    src = sub_once(src, old, new, 'parse_ip')

# 5) init: eth_init + net_init
if 'net_init();' not in src:
    old = '''    puts("Init timer (100 Hz)...\\n");
    timer_init();
'''
    new = '''    puts("Init timer (100 Hz)...\\n");
    timer_init();

    puts("Init eth0...\\n");
    {
        static const uint8_t mac[6] = {0x02,0x00,0x00,0x00,0x00,0x42};
        eth_init(mac);
    }
    puts("  eth0 ready\\n");
'''
    src = sub_once(src, old, new, 'eth_init в kernel_main')

if 'net_init();' not in src:
    old = '''    puts("Initializing MyFS...\\n");'''
    new = '''    puts("Init net...\\n");
    net_init();
    puts("  net ready\\n");

    puts("Initializing MyFS...\\n");'''
    src = sub_once(src, old, new, 'net_init в kernel_main')

# 6) команды шелла
if 'cmd_ifconfig' not in src:
    old = '''    else if (streq(cmd, "sync")) {'''
    new = '''    else if (streq(cmd, "ifconfig")) {
        uint8_t mac[6], ip[4], gw[4];
        eth_get_mac(mac);
        net_get_myip(ip);
        net_get_gwip(gw);
        puts("eth0:\\n");
        puts("  MAC "); for (int i=0;i<6;i++){puthex8(mac[i]); if(i<5)putc(':');}
        puts("\\n  IP  "); net_print_ip(ip);
        puts("\\n  GW  "); net_print_ip(gw);
        puts("\\n");
    }
    else if (streq(cmd, "arp")) { net_show_arp(); }
    else if (streq(cmd, "netstat")) {
        uint32_t t, r, te, re;
        eth_get_stats(&t, &r, &te, &re);
        puts("eth0 stats:\\n");
        puts("  TX ok="); putuint(t); puts("  err="); putuint(te); puts("\\n");
        puts("  RX ok="); putuint(r); puts("  err="); putuint(re); puts("\\n");
        puts("  queue="); putuint(ETH_RX_COUNT); puts("\\n");
    }
    else if (streq(cmd, "ping")) {
        uint8_t ip[4];
        uint32_t n = 3;
        if (!arg[0]) { puts("usage: ping <a.b.c.d> [count]\\n"); }
        else if (!parse_ip(arg, ip)) puts("bad ip\\n");
        else {
            if (arg2[0]) parse_dec(arg2, &n);
            net_ping(ip, (int)n);
        }
    }
    else if (streq(cmd, "nslookup") || streq(cmd, "dns")) {
        if (!arg[0]) { puts("usage: nslookup <host>\\n"); }
        else {
            uint8_t ip[4];
            if (net_dns_resolve(arg, ip) == 0) {
                puts("  "); puts(arg); puts(" -> ");
                net_print_ip(ip); puts("\\n");
            }
        }
    }
    else if (streq(cmd, "netpoll")) {
        net_poll();
        puts("polled\\n");
    }
    else if (streq(cmd, "sync")) {'''
    src = sub_once(src, old, new, 'команды net в шелле')

# 7) обновить cmd_help
if 'nslookup' not in src.split('cmd_help')[1][:2000]:
    old = '    puts("  reboot            halt system\\n");'
    new = ('    puts("  reboot            halt system\\n");\n'
           '    puts("  ifconfig          show eth0 config\\n");\n'
           '    puts("  arp               show ARP table\\n");\n'
           '    puts("  netstat           network stats\\n");\n'
           '    puts("  ping <ip> [n]     ICMP echo\\n");\n'
           '    puts("  nslookup <host>   DNS resolve\\n");\n'
           '    puts("  netpoll           poll eth RX\\n");')
    src = sub_once(src, old, new, 'help: команды net')

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
