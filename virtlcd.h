// virtlcd.h — PL111 LCD controller с true-color ANSI рендерингом
//   * Каждый символ = 2 пикселя (Unicode half-block U+2580)
//   * 24-bit RGB через \033[38;2;R;G;Bm (foreground) и \033[48;2;R;G;Bm (background)
//   * Fallback: 256-цветный xterm-palette, если true-color не поддержан
//   * Опционально: notcurses (alt-screen, ещё выше качество)
#ifndef ARM4_VIRTLCD_H
#define ARM4_VIRTLCD_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>

#if defined(__unix__) || defined(__ANDROID__)
  #include <sys/ioctl.h>
  #include <unistd.h>
#endif

#ifdef HAVE_NOTCURSES
  #include <notcurses/notcurses.h>
  #include <locale.h>
#endif

class VirtLCD {
public:
    enum Reg : uint32_t {
        LCDTiming0 = 0x000, LCDTiming1 = 0x004,
        LCDTiming2 = 0x008, LCDTiming3 = 0x00C,
        LCDUPBASE  = 0x010, LCDLPBASE  = 0x014,
        LCDControl = 0x018, LCDIMSC    = 0x01C,
        LCDRIS     = 0x020, LCDMIS     = 0x024,
        LCDICR     = 0x028, LCDUPCURR  = 0x02C,
        LCDLPCURR  = 0x030,
        PERIPH_ID0 = 0xFE0, PERIPH_ID1 = 0xFE4,
        PERIPH_ID2 = 0xFE8, PERIPH_ID3 = 0xFEC,
        PCELL_ID0  = 0xFF0, PCELL_ID1  = 0xFF4,
        PCELL_ID2  = 0xFF8, PCELL_ID3  = 0xFFC,
    };

    static constexpr uint32_t CNTL_LCDEN     = 1u << 0;
    static constexpr uint32_t CNTL_LCDBPP_16 = 4u << 1;
    static constexpr uint32_t CNTL_LCDBPP_32 = 6u << 1;
    static constexpr uint32_t CNTL_LCDTFT    = 1u << 5;
    static constexpr uint32_t CNTL_BGR       = 1u << 8;
    static constexpr uint32_t CNTL_LCDPWR    = 1u << 11;

    // Режимы вывода
    enum Mode { MODE_ASCII = 0, MODE_TRUECOLOR = 1, MODE_NC256 = 2, MODE_NOTCURSES = 3 };

    uint32_t base;
    const char* name;

    uint32_t timing0 = 0, timing1 = 0, timing2 = 0, timing3 = 0;
    uint32_t upbase  = 0, lpbase  = 0;
    uint32_t control = 0;
    uint32_t imsc    = 0, ris = 0, icr = 0;

    int width  = 320;
    int height = 240;

    uint8_t* arm4_mem = nullptr;
    size_t   arm4_mem_size = 0;

    int  mode = MODE_TRUECOLOR;
    bool true_color = true;       // поддерживает ли терминал 24-bit
    bool term_init_done = false;
    bool cursor_hidden = false;

#ifdef HAVE_NOTCURSES
    notcurses* nc = nullptr;
    ncplane*  plane = nullptr;
    bool      nc_ready = false;
#endif

    uint64_t renders = 0, frames_skipped = 0;
    uint32_t last_hash = 0;

    // Терминальные размеры (кэш)
    int term_cols = 80;
    int term_rows = 24;

    VirtLCD(uint32_t b, const char* n) : base(b), name(n) {}
    ~VirtLCD() { shutdown(); }

    bool contains(uint32_t a) const { return a >= base && a < base + 0x1000; }
    void attach_mem(uint8_t* m, size_t sz) { arm4_mem = m; arm4_mem_size = sz; }

    // Определяем размер терминала
    void query_term_size() {
#if defined(__unix__) || defined(__ANDROID__)
        struct winsize ws;
        if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0
            && ws.ws_col > 0 && ws.ws_row > 0) {
            term_cols = ws.ws_col;
            term_rows = ws.ws_row;
            return;
        }
#endif
        term_cols = 80;
        term_rows = 24;
    }

    bool init_term() {
        if (term_init_done) return true;
        query_term_size();

        if (mode == MODE_NOTCURSES) {
#ifdef HAVE_NOTCURSES
            setlocale(LC_ALL, "");
            notcurses_options opts{};
            opts.flags = NCOPTION_SUPPRESS_BANNERS;
            nc = notcurses_init(&opts, stdout);
            if (nc) {
                plane = notcurses_stdplane(nc);
                unsigned r, c;
                ncplane_dim_yx(plane, &r, &c);
                term_rows = (int)r;
                term_cols = (int)c;
                nc_ready = true;
            }
#endif
        } else {
            // ANSI: скрыть курсор
            std::printf("\033[?25l");
            std::fflush(stdout);
            cursor_hidden = true;
        }
        term_init_done = true;
        return true;
    }

    void shutdown() {
#ifdef HAVE_NOTCURSES
        if (nc_ready) { notcurses_stop(nc); nc = nullptr; plane = nullptr; nc_ready = false; }
#endif
        if (cursor_hidden) {
            std::printf("\033[?25h");
            std::fflush(stdout);
            cursor_hidden = false;
        }
        term_init_done = false;
    }

    // ================= Регистры =================
    uint32_t read(uint32_t addr, int width_v) {
        uint32_t off = addr - base;
        uint32_t v = 0;
        switch (off & ~3u) {
            case LCDTiming0: v = timing0; break;
            case LCDTiming1: v = timing1; break;
            case LCDTiming2: v = timing2; break;
            case LCDTiming3: v = timing3; break;
            case LCDUPBASE:  v = upbase;  break;
            case LCDLPBASE:  v = lpbase;  break;
            case LCDControl: v = control; break;
            case LCDIMSC:    v = imsc;    break;
            case LCDRIS:     v = ris;     break;
            case LCDMIS:     v = ris & imsc; break;
            case LCDICR:     v = 0; break;
            case LCDUPCURR:  v = upbase;  break;
            case LCDLPCURR:  v = lpbase;  break;
            case PERIPH_ID0: v = 0x11; break;
            case PERIPH_ID1: v = 0x11; break;
            case PERIPH_ID2: v = 0x04; break;
            case PERIPH_ID3: v = 0x00; break;
            case PCELL_ID0:  v = 0x0D; break;
            case PCELL_ID1:  v = 0xF0; break;
            case PCELL_ID2:  v = 0x05; break;
            case PCELL_ID3:  v = 0xB1; break;
            default: v = 0;
        }
        if (width_v == 2) v = (v >> ((addr & 2) * 8)) & 0xFFFF;
        else if (width_v == 1) v = (v >> ((addr & 3) * 8)) & 0xFF;
        return v;
    }

    void write(uint32_t addr, uint32_t v, int width_v) {
        (void)width_v;
        uint32_t off = addr - base;
        switch (off & ~3u) {
            case LCDTiming0: timing0 = v; break;
            case LCDTiming1: timing1 = v; break;
            case LCDTiming2: timing2 = v; break;
            case LCDTiming3: timing3 = v; break;
            case LCDUPBASE:  upbase  = v & 0xFFFFFFF0u; break;
            case LCDLPBASE:  lpbase  = v & 0xFFFFFFF0u; break;
            case LCDControl:
                control = v;
                if (v & CNTL_LCDEN) {
                    init_term();
                    render();
                }
                break;
            case LCDIMSC: imsc = v & 0xF; break;
            case LCDICR:  icr = v; ris &= ~v; break;
            default: break;
        }
    }

    // ================= Рендер =================
    int bpp_from_control() const {
        switch ((control >> 1) & 0x7) {
            case 0: return 1; case 1: return 2; case 2: return 4;
            case 3: return 8; case 4: return 16; case 5: return 24;
            case 6: return 32; default: return 16;
        }
    }

    void render() {
        if (!(control & CNTL_LCDEN)) return;
        if (!arm4_mem) return;

        int bpp = bpp_from_control();
        size_t row_bytes = (size_t)width * bpp / 8;
        if (upbase + row_bytes * height > arm4_mem_size) return;

        // Хеш первых байт framebuffer, чтобы не перерисовывать без изменений
        // Полный хеш: сэмплим каждый 64-й байт (покрывает весь фреймбуфер)
        uint32_t h = 0;
        size_t total = row_bytes * height;
        if (total == 0) return;
        size_t step = total / 2048;
        if (step < 64) step = 64;
        for (size_t i = 0; i < total; i += step) {
            if (upbase + i < arm4_mem_size)
                h = h * 31 + arm4_mem[upbase + i];
        }
        if (h == last_hash) { frames_skipped++; return; }
        last_hash = h;

        init_term();

        if (mode == MODE_NOTCURSES) {
#ifdef HAVE_NOTCURSES
            if (nc_ready) {
                ncplane_erase(plane);
                render_notcurses(bpp, row_bytes);
                notcurses_render(nc);
                renders++;
                return;
            }
#endif
            // fallback
            mode = MODE_TRUECOLOR;
        }

        if (mode == MODE_ASCII) render_ascii(bpp, row_bytes);
        else                     render_color(bpp, row_bytes, mode == MODE_NC256);
        renders++;
    }

    // ---- Чтение и усреднение пикселя из framebuffer ----
    uint32_t sample_pixel(int px, int py, int bpp, size_t row_bytes) {
        if (px < 0 || px >= width || py < 0 || py >= height) return 0;
        size_t off = upbase + (size_t)py * row_bytes + (size_t)px * bpp / 8;
        if (off + 4 > arm4_mem_size) return 0;
        switch (bpp) {
            case 16: {
                uint16_t v; std::memcpy(&v, arm4_mem + off, 2);
                int r = ((v >> 11) & 0x1F) * 255 / 31;
                int g = ((v >> 5)  & 0x3F) * 255 / 63;
                int b = ( v        & 0x1F) * 255 / 31;
                return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
            case 24: {
                uint8_t r = arm4_mem[off+0], g = arm4_mem[off+1], b = arm4_mem[off+2];
                if (control & CNTL_BGR) std::swap(r, b);
                return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
            case 32: {
                uint32_t v; std::memcpy(&v, arm4_mem + off, 4);
                return v & 0xFFFFFF;
            }
            case 8: {
                uint8_t v = arm4_mem[off];
                return ((uint32_t)(v & 0xE0) << 16) | ((uint32_t)(v & 0x1C) << 11) | ((uint32_t)(v & 0x03) << 6);
            }
            default: return 0;
        }
    }

    // Усредняет прямоугольник пикселей
    uint32_t avg_region(int x0, int y0, int x1, int y1, int bpp, size_t row_bytes) {
        if (x1 <= x0) x1 = x0 + 1;
        if (y1 <= y0) y1 = y0 + 1;
        uint64_t rs = 0, gs = 0, bs = 0, n = 0;
        for (int py = y0; py < y1 && py < height; py++) {
            for (int px = x0; px < x1 && px < width; px++) {
                uint32_t c = sample_pixel(px, py, bpp, row_bytes);
                rs += (c >> 16) & 0xFF;
                gs += (c >> 8)  & 0xFF;
                bs += c & 0xFF;
                n++;
            }
        }
        if (n == 0) return 0;
        return ((uint32_t)(rs/n) << 16) | ((uint32_t)(gs/n) << 8) | (uint32_t)(bs/n);
    }

    // ---- True-color рендер: half-block, два пикселя на символ ----
    void render_color(int bpp, size_t row_bytes, bool use_256) {
        query_term_size();
        int cols = term_cols;
        int rows = term_rows - 1;         // оставим одну строку для промпта
        if (rows < 4) rows = term_rows;

        int out_rows = rows;              // сколько терминальных строк рисуем
        int pix_rows = out_rows * 2;      // half-block: 2 пикселя на строку

        std::string out;
        out.reserve(cols * out_rows * 32);
        out += "\033[H";                  // курсор домой
        char buf[96];

        for (int ty = 0; ty < out_rows; ty++) {
            int ytop = (ty * 2)     * height / pix_rows;
            int ymid = (ty * 2 + 1) * height / pix_rows;
            int ybot = (ty * 2 + 2) * height / pix_rows;
            if (ymid == ytop) ymid = ytop + 1;
            if (ybot == ymid) ybot = ymid + 1;

            for (int tx = 0; tx < cols; tx++) {
                int x0 = tx * width / cols;
                int x1 = (tx + 1) * width / cols;
                if (x1 == x0) x1 = x0 + 1;

                uint32_t top = avg_region(x0, ytop, x1, ymid, bpp, row_bytes);
                uint32_t bot = avg_region(x0, ymid, x1, ybot, bpp, row_bytes);

                int r1 = (top >> 16) & 0xFF, g1 = (top >> 8) & 0xFF, b1 = top & 0xFF;
                int r2 = (bot >> 16) & 0xFF, g2 = (bot >> 8) & 0xFF, b2 = bot & 0xFF;

                if (use_256) {
                    int c1 = rgb_to_256(r1, g1, b1);
                    int c2 = rgb_to_256(r2, g2, b2);
                    std::snprintf(buf, sizeof(buf),
                        "\033[38;5;%d;48;5;%dm\xE2\x96\x80", c1, c2);
                } else {
                    std::snprintf(buf, sizeof(buf),
                        "\033[38;2;%d;%d;%d;48;2;%d;%d;%dm\xE2\x96\x80",
                        r1, g1, b1, r2, g2, b2);
                }
                out += buf;
            }
            out += "\033[0m\n";
        }
        // Курсор в последнюю строку для промпта
        std::snprintf(buf, sizeof(buf), "\033[%d;1H", term_rows);
        out += buf;

        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
    }

    // ---- Простой ASCII-рендер (fallback для очень старых терминалов) ----
    void render_ascii(int bpp, size_t row_bytes) {
        static const char lum[] = " .:-=+*#%@";
        query_term_size();
        int cols = term_cols;
        int rows = term_rows - 1;
        if (rows < 4) rows = term_rows;

        std::string out = "\033[H";
        for (int ty = 0; ty < rows; ty++) {
            int y0 = ty * height / rows;
            int y1 = (ty + 1) * height / rows;
            if (y1 == y0) y1 = y0 + 1;
            for (int tx = 0; tx < cols; tx++) {
                int x0 = tx * width / cols;
                int x1 = (tx + 1) * width / cols;
                if (x1 == x0) x1 = x0 + 1;
                uint32_t c = avg_region(x0, y0, x1, y1, bpp, row_bytes);
                int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                int lv = (299*r + 587*g + 114*b) / 1000;
                out += lum[lv * 9 / 255];
            }
            out += "\n";
        }
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
    }

    // ---- notcurses (альтернатива, alt-screen) ----
    void render_notcurses(int bpp, size_t row_bytes) {
#ifdef HAVE_NOTCURSES
        int cols = term_cols, rows = term_rows;
        for (int ty = 0; ty < rows; ty++) {
            int y0 = ty * height / rows;
            int y1 = (ty + 1) * height / rows;
            if (y1 == y0) y1 = y0 + 1;
            for (int tx = 0; tx < cols; tx++) {
                int x0 = tx * width / cols;
                int x1 = (tx + 1) * width / cols;
                if (x1 == x0) x1 = x0 + 1;
                uint32_t c = avg_region(x0, y0, x1, y1, bpp, row_bytes);
                int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                ncplane_set_fg_rgb(plane, ((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
                ncplane_set_bg_rgb(plane, ((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
                ncplane_putchar_yx(plane, ty, tx, ' ');
            }
        }
#else
        (void)bpp; (void)row_bytes;
#endif
    }

    // RGB -> 256-цветный индекс
    static int rgb_to_256(int r, int g, int b) {
        // Используем xterm 6x6x6 cube (16..231) + grayscale (232..255)
        // Простой вариант — cube
        int ri = (r * 5 + 127) / 255;
        int gi = (g * 5 + 127) / 255;
        int bi = (b * 5 + 127) / 255;
        return 16 + ri*36 + gi*6 + bi;
    }

    // -------- IRQ --------
    bool irq_pending() const { return (ris & imsc) != 0; }
    void vsync() { ris |= 1; }
    void tick() {}

    void dump() const {
        int bpp = 16;
        switch ((control >> 1) & 0x7) {
            case 0: bpp = 1; break; case 1: bpp = 2; break;
            case 2: bpp = 4; break; case 3: bpp = 8; break;
            case 4: bpp = 16; break; case 5: bpp = 24; break;
            case 6: bpp = 32; break; default: bpp = 16;
        }
        std::fprintf(stderr, "--- PL111 LCD @ 0x%08X (%s) ---\n", base, name);
        std::fprintf(stderr, "  Enabled=%d  BPP=%d  Size=%dx%d  UPBASE=0x%08X  LPBASE=0x%08X\n",
                     (int)((control & CNTL_LCDEN) != 0), bpp, width, height, upbase, lpbase);
        std::fprintf(stderr, "  IMSC=0x%X RIS=0x%X MIS=0x%X\n", imsc, ris, ris & imsc);
        std::fprintf(stderr, "  Renders=%llu  Skipped=%llu  Term=%dx%d\n",
                     (unsigned long long)renders,
                     (unsigned long long)frames_skipped, term_cols, term_rows);
        const char* mname = "?";
        switch (mode) {
            case MODE_ASCII:     mname = "ascii"; break;
            case MODE_TRUECOLOR: mname = "truecolor"; break;
            case MODE_NC256:     mname = "256color"; break;
            case MODE_NOTCURSES: mname = "notcurses"; break;
        }
        std::fprintf(stderr, "  Mode=%s  Backend=%s\n",
                     mname,
#ifdef HAVE_NOTCURSES
                     nc_ready ? "notcurses" : (mode == MODE_NOTCURSES ? "ansi-fallback" : "ansi")
#else
                     "ansi"
#endif
        );
    }
};

#endif // ARM4_VIRTLCD_H
