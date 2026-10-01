import pathlib
p = pathlib.Path("kernel.c")
src = p.read_text()

old = '''    else if (streq(cmd, "ping")) {
        uint8_t ip[4];
        uint32_t n = 3;
        if (!arg[0]) { puts("usage: ping <a.b.c.d> [count]\\n"); }
        else if (!parse_ip(arg, ip)) puts("bad ip\\n");
        else {
            if (arg2[0]) parse_dec(arg2, &n);
            net_ping(ip, (int)n);
        }
    }'''

new = '''    else if (streq(cmd, "ping")) {
        uint8_t ip[4];
        uint32_t n = 3;
        if (!arg[0]) { puts("usage: ping <a.b.c.d> [count]\\n"); }
        else {
            uint32_t l = 0; while (arg[l]) l++;
            puts("[DBG] arg len="); putuint(l); puts(": '");
            for (uint32_t i = 0; i < l; i++) {
                uint8_t ch = (uint8_t)arg[i];
                if (ch >= 32 && ch < 127) putc((char)ch);
                else { putc('<'); puthex8(ch); putc('>'); }
            }
            puts("'\\n");
            if (!parse_ip(arg, ip)) puts("bad ip\\n");
            else {
                if (arg2[0]) parse_dec(arg2, &n);
                net_ping(ip, (int)n);
            }
        }
    }'''

if old in src:
    src = src.replace(old, new, 1)
    print("[+] debug в ping")
else:
    print("[!] не найден ping branch")
p.write_text(src)
