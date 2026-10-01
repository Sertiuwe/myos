import pathlib
p = pathlib.Path("vmcu.h")
src = p.read_text()
ok = True

def sub(s, old, new, tag):
    global ok
    if old not in s:
        print(f"[!] НЕТ: {tag}"); ok = False; return s
    if s.count(old) != 1:
        print(f"[!] НЕОДНОЗНАЧНО ({s.count(old)}): {tag}"); ok = False; return s
    print(f"[+] OK: {tag}")
    return s.replace(old, new, 1)

# 1) Регистры GFX
old = '''        R_DMA_STAT   = 0x64,   // 0=idle, 1=done, 2=error
    };'''
new = '''        R_DMA_STAT   = 0x64,   // 0=idle, 1=done, 2=error
        // --- Graphics (framebuffer ARM4) ---
        R_FB_BASE    = 0x68,   // адрес framebuffer в ARM4 памяти
        R_GFX_CMD    = 0x6C,   // 0=nop 1=pixel 2=fill 3=rect 4=clear 5=hline 6=vline
        R_GFX_X0     = 0x70,
        R_GFX_Y0     = 0x74,
        R_GFX_X1     = 0x78,
        R_GFX_Y1     = 0x7C,
        R_GFX_COLOR  = 0x80,   // RGB565
    };'''
src = sub(src, old, new, "регистры GFX")

# 2) Поля
old = '''    uint32_t dma_stat = 0;

    // Внешние указатели'''
new = '''    uint32_t dma_stat = 0;

    // Graphics
    uint32_t fb_base   = 0;
    uint32_t gfx_x0    = 0, gfx_y0 = 0;
    uint32_t gfx_x1    = 0, gfx_y1 = 0;
    uint32_t gfx_color = 0;
    uint64_t gfx_ops   = 0;

    // Внешние указатели'''
src = sub(src, old, new, "поля GFX")

# 3) Метод gfx_exec — вставим перед do_dma
old = '''    // ---------- DMA ----------
    void do_dma() {'''
new = '''    // ---------- Graphics ----------
    static constexpr int FB_W = 320;
    static constexpr int FB_H = 240;

    inline void fb_write(int x, int y, uint16_t c) {
        if (x < 0 || x >= FB_W || y < 0 || y >= FB_H) return;
        if (!arm4_mem) return;
        size_t off = fb_base + (size_t)y * FB_W * 2 + (size_t)x * 2;
        if (off + 2 > arm4_mem_size) return;
        std::memcpy(arm4_mem + off, &c, 2);
    }

    void gfx_exec(uint32_t cmd) {
        if (!arm4_mem) return;
        gfx_ops++;
        uint16_t color = (uint16_t)gfx_color;
        switch (cmd) {
            case 0: break;  // nop
            case 1: fb_write((int)gfx_x0, (int)gfx_y0, color); break;
            case 2:  // fill всё
                for (int y = 0; y < FB_H; y++)
                    for (int x = 0; x < FB_W; x++)
                        fb_write(x, y, color);
                break;
            case 3: {  // rect
                int x0 = (int)gfx_x0, y0 = (int)gfx_y0;
                int x1 = (int)gfx_x1, y1 = (int)gfx_y1;
                if (x0 > x1) std::swap(x0, x1);
                if (y0 > y1) std::swap(y0, y1);
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++)
                        fb_write(x, y, color);
                break;
            }
            case 4:  // clear (fill 0)
                for (int y = 0; y < FB_H; y++)
                    for (int x = 0; x < FB_W; x++)
                        fb_write(x, y, 0);
                break;
            case 5: {  // hline
                int x0 = (int)gfx_x0, x1 = (int)gfx_x1;
                if (x0 > x1) std::swap(x0, x1);
                for (int x = x0; x < x1; x++)
                    fb_write(x, (int)gfx_y0, color);
                break;
            }
            case 6: {  // vline
                int y0 = (int)gfx_y0, y1 = (int)gfx_y1;
                if (y0 > y1) std::swap(y0, y1);
                for (int y = y0; y < y1; y++)
                    fb_write((int)gfx_x0, y, color);
                break;
            }
            default: break;
        }
    }

    // ---------- DMA ----------
    void do_dma() {'''
src = sub(src, old, new, "метод gfx_exec")

# 4) read — GFX регистры
old = '''            case R_DMA_GO:   v = 0; break;  // write-only
            case R_DMA_STAT: v = dma_stat; break;'''
new = '''            case R_DMA_GO:   v = 0; break;  // write-only
            case R_DMA_STAT: v = dma_stat; break;
            case R_FB_BASE:   v = fb_base; break;
            case R_GFX_CMD:   v = 0; break;
            case R_GFX_X0:    v = gfx_x0; break;
            case R_GFX_Y0:    v = gfx_y0; break;
            case R_GFX_X1:    v = gfx_x1; break;
            case R_GFX_Y1:    v = gfx_y1; break;
            case R_GFX_COLOR: v = gfx_color; break;'''
src = sub(src, old, new, "read GFX")

# 5) write — GFX регистры
old = '''            case R_DMA_GO:   if (v & 1) do_dma(); break;
            case R_DMA_STAT: dma_stat = v; break;
        }
    }'''
new = '''            case R_DMA_GO:   if (v & 1) do_dma(); break;
            case R_DMA_STAT: dma_stat = v; break;
            case R_FB_BASE:   fb_base = v; break;
            case R_GFX_CMD:   gfx_exec(v); break;
            case R_GFX_X0:    gfx_x0 = v; break;
            case R_GFX_Y0:    gfx_y0 = v; break;
            case R_GFX_X1:    gfx_x1 = v; break;
            case R_GFX_Y1:    gfx_y1 = v; break;
            case R_GFX_COLOR: gfx_color = v; break;
        }
    }'''
src = sub(src, old, new, "write GFX")

# 6) dump — добавить строку про GFX
old = '''        std::printf("  DMA: src=0x%X dst=0x%X len=%u dir=%u stat=%u ops=%llu bytes=%llu\\n",'''
new = '''        std::printf("  GFX: fb=0x%08X color=0x%04X ops=%llu\\n",
                    fb_base, gfx_color, (unsigned long long)gfx_ops);
        std::printf("  DMA: src=0x%X dst=0x%X len=%u dir=%u stat=%u ops=%llu bytes=%llu\\n",'''
src = sub(src, old, new, "dump GFX")

p.write_text(src)
print("РЕЗУЛЬТАТ:", "OK" if ok else "ЧАСТИЧНО")
