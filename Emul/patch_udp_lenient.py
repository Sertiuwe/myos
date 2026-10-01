import pathlib

p = pathlib.Path("virteth.h")
src = p.read_text()

old = '''        if (ulen < 8 || ulen > ip_len - ihl) return;
        const uint8_t* payload = udp + 8;
        size_t plen = ulen - 8;
'''
new = '''        if (ulen < 8 || ulen > ip_len - ihl) {
            if (sniff) std::printf("[ETH-BRIDGE] fix ulen: %u -> %zu\\n",
                                   ulen, ip_len - ihl);
            ulen = (uint16_t)(ip_len - ihl);
            if (ulen < 8) return;
        }
        const uint8_t* payload = udp + 8;
        size_t plen = ulen - 8;
'''
src = src.replace(old, new, 1) if old in src else src
print("[+] lenient")

old2 = '''        ssize_t s = ::sendto(udp_bridge_sock, payload, plen, 0,
                             (struct sockaddr*)&dst, sizeof(dst));'''
new2 = '''        if (sniff) std::printf("[ETH-BRIDGE] try -> %s:%u, %zu байт\\n",
                               inet_ntoa(dst.sin_addr), dport, plen);
        ssize_t s = ::sendto(udp_bridge_sock, payload, plen, 0,
                             (struct sockaddr*)&dst, sizeof(dst));'''
src = src.replace(old2, new2, 1) if old2 in src else src
print("[+] send debug")

p.write_text(src)
