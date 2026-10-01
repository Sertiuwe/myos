import pathlib
p = pathlib.Path("vmcu.h")
src = p.read_text()

old = '''    void io_write(uint32_t port, uint32_t v) {
        switch (port) {
            case 0: res0 = v; break;
            case 1: res1 = v; break;
            case 2: irq_stat |= 1u; break;
            default: break;
        }
    }'''

new = '''    void io_write(uint32_t port, uint32_t v) {
        switch (port) {
            case 0: res0 = v; break;
            case 1: res1 = v; break;
            case 2: irq_stat |= 1u; break;
            // GFX-порты (для use из VMCU-программ)
            case 5:  fb_base   = v; break;
            case 6:  gfx_exec(v); break;
            case 7:  gfx_x0    = v; break;
            case 8:  gfx_y0    = v; break;
            case 9:  gfx_x1    = v; break;
            case 10: gfx_y1    = v; break;
            case 11: gfx_color = v; break;
            default: break;
        }
    }'''

if old in src:
    src = src.replace(old, new, 1)
    print("[+] io_write расширен GFX-портами")
else:
    print("[!] io_write не найден")

p.write_text(src)
