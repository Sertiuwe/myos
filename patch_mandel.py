import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

old = '''            if (std::strcmp(arg1, "mandel") == 0) {
                float zoom = arg2[0] ? std::strtof(arg2, nullptr) : 1.0f;
                for (int y = 0; y < 240; y++) {
                    for (int x = 0; x < 320; x++) {
                        float cr = (x - 240) / (120.0f * zoom) - 0.5f;
                        float ci = (y - 120) / (120.0f * zoom);
                        float zr = 0, zi = 0;
                        int it = 0;
                        const int MAX_ITER = 60;
                        while (zr*zr + zi*zi < 4.0f && it < MAX_ITER) {
                            float t = zr*zr - zi*zi + cr;
                            zi = 2*zr*zi + ci;
                            zr = t;
                            it++;
                        }
                        int v = it * 255 / MAX_ITER;
                        int r = (v >> 3);
                        int g = (v * 2 > 63 ? 63 : v * 2);
                        int b = (v >> 3);
                        uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
                        std::memcpy(emu.mem.data.data() + L.upbase + y*640 + x*2, &c, 2);
                    }
                }
                L.init_term(); L.last_hash = 0; L.render();
                std::fprintf(stderr, "LCD mandelbrot zoom=%.2f, frames=%llu\\n",
                            (double)zoom, (unsigned long long)L.renders);
                return false;
            }'''

new = '''            if (std::strcmp(arg1, "mandel") == 0) {
                /* mandel [zoom] [cx] [cy] */
                char za[32]={0}, xa[32]={0}, ya[32]={0};
                std::sscanf(line_str.c_str(), "%*s %*s %31s %31s %31s", za, xa, ya);
                float zoom = za[0] ? std::strtof(za, nullptr) : 1.0f;
                float cx   = xa[0] ? std::strtof(xa, nullptr) : -0.5f;
                float cy   = ya[0] ? std::strtof(ya, nullptr) : 0.0f;
                if (zoom < 0.1f) zoom = 0.1f;

                const int MAX_ITER = 120;
                const float scale = 100.0f * zoom;

                for (int y = 0; y < 240; y++) {
                    for (int x = 0; x < 320; x++) {
                        float cr = (x - 160) / scale + cx;
                        float ci = (y - 120) / scale + cy;
                        float zr = 0, zi = 0, zr2 = 0, zi2 = 0;
                        int it = 0;
                        while (zr2 + zi2 <= 4.0f && it < MAX_ITER) {
                            zi = 2*zr*zi + ci;
                            zr = zr2 - zi2 + cr;
                            zr2 = zr*zr;
                            zi2 = zi*zi;
                            it++;
                        }
                        int r, g, b;
                        if (it >= MAX_ITER) {
                            r = 0; g = 0; b = 0;              // внутри — чёрный
                        } else {
                            /* Smooth coloring: сглаживаем через log */
                            float m2 = zr2 + zi2;
                            float sm = (float)it - std::log2(0.5f * std::log(m2));
                            if (sm < 0) sm = 0;
                            float t = sm * 0.05f;             // частота
                            /* Классическая палитра: тёмно-синий -> голубой -> белый -> оранжевый */
                            float w = 0.5f + 0.5f * std::cos(6.28318f * t);
                            r = (int)((1.0f - w) * 31);
                            g = (int)(std::sin(6.28318f * t * 1.5f) * 0.5f * 63 + 32);
                            b = (int)(w * 31);
                            if (r < 0) r = 0; if (r > 31) r = 31;
                            if (g < 0) g = 0; if (g > 63) g = 63;
                            if (b < 0) b = 0; if (b > 31) b = 31;
                        }
                        uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
                        std::memcpy(emu.mem.data.data() + L.upbase + y*640 + x*2, &c, 2);
                    }
                }
                L.init_term(); L.last_hash = 0; L.render();
                std::fprintf(stderr, "LCD mandelbrot zoom=%.2f center=(%.3f,%.3f) frames=%llu\\n",
                            (double)zoom, (double)cx, (double)cy,
                            (unsigned long long)L.renders);
                return false;
            }'''

if old not in src:
    print("[!] mandel-блок не найден"); raise SystemExit(1)
src = src.replace(old, new, 1)
p.write_text(src)
print("[+] mandelbrot: центр + smooth coloring + палитра")
