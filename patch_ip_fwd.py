import pathlib
p = pathlib.Path("kernel.c")
src = p.read_text()

# forward declare parse_dec перед parse_ip
old = 'static int parse_ip(const char *s, uint8_t out[4]) {'
new = 'static int parse_dec(const char *s, uint32_t *out);\n\nstatic int parse_ip(const char *s, uint8_t out[4]) {'

if 'static int parse_dec(const char *s, uint32_t *out);' not in src:
    src = src.replace(old, new, 1)
    print("[+] forward declaration parse_dec")
else:
    print("[=] уже есть")

p.write_text(src)
