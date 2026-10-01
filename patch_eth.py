import pathlib

p = pathlib.Path("ARM4.cpp")
src = p.read_text()
ok = True

def sub_once(s, old, new, tag):
    global ok
    if old not in s:
        print(f"[!] нет: {tag}"); ok = False; return s
    if s.count(old) != 1:
        print(f"[!] неоднозначно ({s.count(old)}): {tag}"); ok = False; return s
    print(f"[+] ok: {tag}")
    return s.replace(old, new, 1)

if '#include "virteth.h"' not in src:
    src = sub_once(src, '#include "virtnet.h"\n',
                   '#include "virtnet.h"\n#include "virteth.h"\n',
                   'include virteth.h')
if '#include <deque>' not in src:
    src = sub_once(src, '#include <algorithm>\n',
                   '#include <algorithm>\n#include <deque>\n',
                   'include deque')

if 'VirtEth   eth{' not in src:
    src = sub_once(src,
        '    VirtNet    net{0x10015000, "NET0"};\n',
        '    VirtNet    net{0x10015000, "NET0"};\n'
        '    VirtEth    eth{0x10016000, "ETH0"};\n',
        'member eth')

# MMIO routing
pairs = [
 ('if (emu->net.contains(a))    return emu->net.read(a, 4);',
  'if (emu->net.contains(a))    return emu->net.read(a, 4);\n'
  '        if (emu->eth.contains(a))    return emu->eth.read(a, 4);', 'read32'),
 ('if (emu->net.contains(a))    return (uint16_t)emu->net.read(a, 2);',
  'if (emu->net.contains(a))    return (uint16_t)emu->net.read(a, 2);\n'
  '        if (emu->eth.contains(a))    return (uint16_t)emu->eth.read(a, 2);', 'read16'),
 ('if (emu->net.contains(a))    return (uint8_t)emu->net.read(a, 1);',
  'if (emu->net.contains(a))    return (uint8_t)emu->net.read(a, 1);\n'
  '        if (emu->eth.contains(a))    return (uint8_t)emu->eth.read(a, 1);', 'read8'),
 ('if (emu->net.contains(a))    { emu->net.write(a, v, 4); return; }',
  'if (emu->net.contains(a))    { emu->net.write(a, v, 4); return; }\n'
  '        if (emu->eth.contains(a))    { emu->eth.write(a, v, 4); return; }', 'write32'),
 ('if (emu->net.contains(a))    { emu->net.write(a, v, 2); return; }',
  'if (emu->net.contains(a))    { emu->net.write(a, v, 2); return; }\n'
  '        if (emu->eth.contains(a))    { emu->eth.write(a, v, 2); return; }', 'write16'),
 ('if (emu->net.contains(a))    { emu->net.write(a, v, 1); return; }',
  'if (emu->net.contains(a))    { emu->net.write(a, v, 1); return; }\n'
  '        if (emu->eth.contains(a))    { emu->eth.write(a, v, 1); return; }', 'write8'),
]
for old, new, tag in pairs:
    tail = new.strip().splitlines()[-1].strip()
    if tail in src:
        print(f"[=] {tag} уже пропатчен"); continue
    src = sub_once(src, old, new, tag)

# tick
if 'eth.tick();' not in src:
    src = sub_once(src, 'net.tick();\n',
                   'net.tick();\n        eth.tick();\n', 'eth.tick')

# VIC source 15
if 'vic.set_source(15,' not in src:
    src = sub_once(src, 'vic.set_source(14, net.irq_pending());\n',
                   'vic.set_source(14, net.irq_pending());\n'
                   '        vic.set_source(15, eth.irq_pending());\n',
                   'VIC source 15')

# команды eth
if '"eth dump' not in src:
    eth_cmds = r'''        if (std::strcmp(cmd, "eth") == 0) {
            VirtEth& e = emu.eth;
            if (!arg1[0] || std::strcmp(arg1, "dump") == 0) { e.dump(); return false; }
            if (std::strcmp(arg1, "mac") == 0) {
                if (arg2[0]) {
                    unsigned a,b,c,d,f,g;
                    if (std::sscanf(arg2, "%x:%x:%x:%x:%x:%x", &a,&b,&c,&d,&f,&g) == 6) {
                        e.mac[0]=a; e.mac[1]=b; e.mac[2]=c;
                        e.mac[3]=d; e.mac[4]=f; e.mac[5]=g;
                    }
                }
                char m[32]; e.mac_str(m);
                std::printf("MAC = %s\n", m);
                return false;
            }
            if (std::strcmp(arg1, "on") == 0)  { e.enable = true;  std::printf("eth enable\n"); return false; }
            if (std::strcmp(arg1, "off") == 0) { e.enable = false; std::printf("eth disable\n"); return false; }
            if (std::strcmp(arg1, "promisc") == 0) {
                e.promisc = arg2[0] ? (std::atoi(arg2)!=0) : !e.promisc;
                std::printf("promisc=%d\n", (int)e.promisc); return false;
            }
            if (std::strcmp(arg1, "loopback") == 0) {
                e.loopback = arg2[0] ? (std::atoi(arg2)!=0) : !e.loopback;
                std::printf("loopback=%d\n", (int)e.loopback); return false;
            }
            if (std::strcmp(arg1, "irq") == 0) {
                if (!arg2[0]) { std::printf("irq stat=0x%X mask=0x%X\n", e.irq_stat, e.irq_mask); return false; }
                e.irq_mask = parse_u32(arg2); std::printf("irq_mask=0x%X\n", e.irq_mask); return false;
            }
            if (std::strcmp(arg1, "iren") == 0) { e.irq_en = true;  std::printf("irq_en=1\n"); return false; }
            if (std::strcmp(arg1, "iroff") == 0){ e.irq_en = false; std::printf("irq_en=0\n"); return false; }
            if (std::strcmp(arg1, "sniff") == 0) {
                e.sniff = arg2[0] ? (std::atoi(arg2)!=0) : !e.sniff;
                std::printf("sniff=%d\n", (int)e.sniff); return false;
            }
            if (std::strcmp(arg1, "sendhex") == 0) {
                const char* p = line_str.c_str();
                while (*p && *p != ' ') p++; while (*p == ' ') p++;
                while (*p && *p != ' ') p++; while (*p == ' ') p++;
                size_t len = std::strlen(p);
                if (len == 0 || (len & 1)) { std::printf("eth sendhex <hex>\n"); return false; }
                std::vector<uint8_t> bin; bin.reserve(len/2);
                auto hv=[](char c)->int{ if(c>='0'&&c<='9')return c-'0';
                    if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; };
                for (size_t i=0;i<len;i+=2){ int h=hv(p[i]),l=hv(p[i+1]);
                    if(h<0||l<0){ std::printf("не hex\n"); return false; }
                    bin.push_back((uint8_t)((h<<4)|l)); }
                e.transmit_frame(bin.data(), bin.size());
                return false;
            }
            if (std::strcmp(arg1, "inject") == 0) {
                const char* p = line_str.c_str();
                while (*p && *p != ' ') p++; while (*p == ' ') p++;
                while (*p && *p != ' ') p++; while (*p == ' ') p++;
                size_t len = std::strlen(p);
                if (len == 0 || (len & 1)) { std::printf("eth inject <hex>\n"); return false; }
                std::vector<uint8_t> bin; bin.reserve(len/2);
                auto hv=[](char c)->int{ if(c>='0'&&c<='9')return c-'0';
                    if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; };
                for (size_t i=0;i<len;i+=2){ int h=hv(p[i]),l=hv(p[i+1]);
                    if(h<0||l<0){ std::printf("не hex\n"); return false; }
                    bin.push_back((uint8_t)((h<<4)|l)); }
                e.receive_frame(bin.data(), bin.size());
                return false;
            }
            if (std::strcmp(arg1, "rx") == 0) {
                std::printf("RX count=%zu cur=%zu/%zu\n",
                    e.rx_queue.size(), e.rx_pos, e.rx_current.size());
                return false;
            }
            if (std::strcmp(arg1, "rxhex") == 0) {
                if (e.rx_current.empty() && !e.rx_queue.empty()) {
                    e.rx_current = e.rx_queue.front().data; e.rx_queue.pop_front(); e.rx_pos = 0;
                }
                std::printf("RX cur: %zu байт: ", e.rx_current.size());
                for (size_t i = 0; i < e.rx_current.size(); i++)
                    std::printf("%02X", e.rx_current[i]);
                std::printf("\n");
                return false;
            }
            std::printf("eth dump|mac XX:..|on|off|promisc|loopback|iren|iroff|irq [m]|sniff|sendhex|inject|rx|rxhex\n");
            return false;
        }
'''
    anchor = '        if (std::strcmp(cmd, "uart0") == 0) {\n'
    if anchor in src:
        src = src.replace(anchor, eth_cmds + anchor, 1)
        print("[+] ok: команды eth добавлены")
    else:
        print("[!] не найден anchor для eth"); ok = False

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично")
