import pathlib, sys

p = pathlib.Path("ARM4.cpp")
src = p.read_text()

def sub_once(s, old, new, what):
    if old not in s:
        print(f"[!] не найдено: {what}")
        return s, False
    if s.count(old) != 1:
        print(f"[!] неоднозначно ({s.count(old)} раз): {what}")
        return s, False
    print(f"[+] ok: {what}")
    return s.replace(old, new, 1), True

ok = True

# 1) include
if '#include "virtnet.h"' not in src:
    src, r = sub_once(src,
        '#include <algorithm>\n',
        '#include <algorithm>\n\n#include "virtnet.h"\n',
        'include virtnet.h'); ok &= r
else:
    print("[=] include уже есть")

# 2) член Emulator
if 'VirtNet    net{' not in src:
    src, r = sub_once(src,
        '    PL190_VIC  vic{0x10140000, "VIC"};\n',
        '    PL190_VIC  vic{0x10140000, "VIC"};\n'
        '    VirtNet    net{0x10015000, "NET0"};\n',
        'member VirtNet net'); ok &= r
else:
    print("[=] member net уже есть")

# 3) маршрутизация MMIO
routing = [
    ('if (emu->gpio1.contains(a))  return emu->gpio1.read(a, 4);',
     'if (emu->gpio1.contains(a))  return emu->gpio1.read(a, 4);\n'
     '        if (emu->net.contains(a))    return emu->net.read(a, 4);',
     'read32'),
    ('if (emu->gpio1.contains(a))  return (uint16_t)emu->gpio1.read(a, 2);',
     'if (emu->gpio1.contains(a))  return (uint16_t)emu->gpio1.read(a, 2);\n'
     '        if (emu->net.contains(a))    return (uint16_t)emu->net.read(a, 2);',
     'read16'),
    ('if (emu->gpio1.contains(a))  return (uint8_t)emu->gpio1.read(a, 1);',
     'if (emu->gpio1.contains(a))  return (uint8_t)emu->gpio1.read(a, 1);\n'
     '        if (emu->net.contains(a))    return (uint8_t)emu->net.read(a, 1);',
     'read8'),
    ('if (emu->gpio1.contains(a))  { emu->gpio1.write(a, v, 4); return; }',
     'if (emu->gpio1.contains(a))  { emu->gpio1.write(a, v, 4); return; }\n'
     '        if (emu->net.contains(a))    { emu->net.write(a, v, 4); return; }',
     'write32'),
    ('if (emu->gpio1.contains(a))  { emu->gpio1.write(a, v, 2); return; }',
     'if (emu->gpio1.contains(a))  { emu->gpio1.write(a, v, 2); return; }\n'
     '        if (emu->net.contains(a))    { emu->net.write(a, v, 2); return; }',
     'write16'),
    ('if (emu->gpio1.contains(a))  { emu->gpio1.write(a, v, 1); return; }',
     'if (emu->gpio1.contains(a))  { emu->gpio1.write(a, v, 1); return; }\n'
     '        if (emu->net.contains(a))    { emu->net.write(a, v, 1); return; }',
     'write8'),
]
for old, new, tag in routing:
    if 'emu->net.contains(a)' in src and tag in ('read32',):
        pass
    if new.strip().splitlines()[-1].strip() in src:
        print(f"[=] {tag} уже пропатчен")
        continue
    src, r = sub_once(src, old, new, tag); ok &= r

# 4) tick в step()
if 'net.tick();' not in src:
    src, r = sub_once(src,
        'gpio0.irq_pending());\n',
        'gpio0.irq_pending());\n        net.tick();\n',
        'net.tick() в step()'); ok &= r
else:
    print("[=] net.tick() уже есть")

# 5) IRQ от сети в VIC (источник 14)
if 'vic.set_source(14,' not in src:
    src, r = sub_once(src,
        'vic.set_source(6,  gpio0.irq_pending());\n',
        'vic.set_source(6,  gpio0.irq_pending());\n'
        '        vic.set_source(14, net.irq_pending());\n',
        'VIC source 14 для сети'); ok &= r

# 6) команды net в command_handler
if '"net dump' not in src:
    net_cmds = '''        if (std::strcmp(cmd, "net") == 0) {
            if (!arg1[0] || std::strcmp(arg1, "dump") == 0) { emu.net.dump(); return false; }
            if (std::strcmp(arg1, "open") == 0)  { emu.net.ctrl |= 1; emu.net.open_socket(); return false; }
            if (std::strcmp(arg1, "close") == 0) { emu.net.ctrl &= ~1u; emu.net.close_socket(); return false; }
            if (std::strcmp(arg1, "send") == 0) {
                const char* p = line_str.c_str() + 4;
                while (*p == ' ') p++;
                emu.net.send_packet((const uint8_t*)p, std::strlen(p));
                return false;
            }
            if (std::strcmp(arg1, "peer") == 0) {
                char ip[32]={0}, port[16]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %31s %15s", ip, port);
                if (ip[0])   emu.net.peer_ip   = inet_addr(ip);
                if (port[0]) emu.net.peer_port = (uint32_t)std::atoi(port);
                std::printf("peer = 0x%08X : %u\\n", emu.net.peer_ip, emu.net.peer_port);
                return false;
            }
            if (std::strcmp(arg1, "loopback") == 0) {
                emu.net.loopback = arg2[0] ? (std::atoi(arg2) != 0) : !emu.net.loopback;
                std::printf("loopback = %d\\n", (int)emu.net.loopback);
                return false;
            }
            if (std::strcmp(arg1, "port") == 0) {
                char lp[16]={0}, pp[16]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %15s %15s", lp, pp);
                if (lp[0]) emu.net.local_port = (uint32_t)std::atoi(lp);
                if (pp[0]) emu.net.peer_port  = (uint32_t)std::atoi(pp);
                emu.net.close_socket();
                if (emu.net.ctrl & 1) emu.net.open_socket();
                std::printf("local=%u peer=%u\\n", emu.net.local_port, emu.net.peer_port);
                return false;
            }
            std::printf("net dump|open|close|send <текст>|peer <ip> <port>|port <l> <p>|loopback [0|1]\\n");
            return false;
        }
'''
    anchor = '        if (std::strcmp(cmd, "uart0") == 0) {\n'
    if anchor in src:
        src = src.replace(anchor, net_cmds + anchor, 1)
        print("[+] ok: команды net добавлены")
    else:
        print("[!] не найден anchor для команд net"); ok = False

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "частично, проверь сообщения выше")
