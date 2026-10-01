import pathlib
p = pathlib.Path("kernel.c")
src = p.read_text()

old = '''static int parse_ip(const char *s, uint8_t out[4]) {
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
}'''

new = '''static int parse_ip(const char *s, uint8_t out[4]) {
    char tmp[20];
    int i = 0;
    while (s[i] && i < 19) { tmp[i] = s[i]; i++; }
    tmp[i] = 0;

    int n = 0;
    char *p = tmp;
    while (n < 4) {
        char buf[4];
        int bi = 0;
        while (*p >= '0' && *p <= '9' && bi < 3) buf[bi++] = *p++;
        buf[bi] = 0;
        if (bi == 0) return 0;
        uint32_t v = 0;
        if (!parse_dec(buf, &v)) return 0;
        if (v > 255) return 0;
        out[n++] = (uint8_t)v;
        if (*p == 0) break;
        if (*p != '.') return 0;
        p++;
    }
    return (n == 4 && *p == 0);
}'''

if old in src:
    src = src.replace(old, new, 1)
    print("[+] parse_ip переписан")
else:
    print("[!] parse_ip не найден")
p.write_text(src)
