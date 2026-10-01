import pathlib
p = pathlib.Path("ARM4.cpp")
src = p.read_text()

# Вставим lcd life перед финальным help-строкой в lcd-блоке
old = '''            std::fprintf(stderr, "lcd dump|mode [..]|on|off|render|fb <a>|fill <c>|pixel <x> <y> <c>|gradient|checker|bars|plasma [t]|anim [n]|mandel [zoom]|screenshot <file>\\n");
            return false;
        }'''

new = '''            // ==================== Game of Life ====================
            if (std::strcmp(arg1, "life") == 0) {
                int gens = arg2[0] ? std::atoi(arg2) : 50;
                if (gens < 1)   gens = 1;
                if (gens > 500) gens = 500;

                const int W = 80, H = 60;          // клетки
                const int SCALE = 4;                // 4x4 пикселя на клетку
                std::vector<uint8_t> cur(W*H), nxt(W*H);

                // Начальная конфигурация: 25% случайных
                uint32_t rng = 0xC0FFEE42u;
                for (int i = 0; i < W*H; i++) {
                    rng = rng * 1664525u + 1013904223u;
                    cur[i] = ((rng >> 20) & 0x3) == 0 ? 1 : 0;
                }

                L.init_term();

                auto draw_field = [&](void) {
                    for (int y = 0; y < 240; y++) {
                        int cy = y / SCALE;
                        uint8_t* row = emu.mem.data.data() + L.upbase + y*640;
                        for (int x = 0; x < 320; x++) {
                            int cx = x / SCALE;
                            uint16_t c = cur[cy*W + cx] ? 0xFFFF : 0x0000;
                            std::memcpy(row + x*2, &c, 2);
                        }
                    }
                };

                for (int g = 0; g < gens; g++) {
                    // Отрисовать текущее поколение
                    draw_field();
                    L.last_hash = 0;
                    L.render();

                    // Посчитать следующее
                    for (int y = 0; y < H; y++) {
                        for (int x = 0; x < W; x++) {
                            int n = 0;
                            for (int dy = -1; dy <= 1; dy++) {
                                for (int dx = -1; dx <= 1; dx++) {
                                    if (dx == 0 && dy == 0) continue;
                                    int nx = (x + dx + W) % W;
                                    int ny = (y + dy + H) % H;
                                    n += cur[ny*W + nx];
                                }
                            }
                            int alive = cur[y*W + x];
                            nxt[y*W + x] =
                                (alive ? (n == 2 || n == 3) : (n == 3)) ? 1 : 0;
                        }
                    }
                    std::swap(cur, nxt);
                }

                // Финальный кадр
                draw_field();
                L.last_hash = 0;
                L.render();
                std::fprintf(stderr, "LCD life: %d generations on %dx%d grid (%llu frames)\\n",
                            gens, W, H, (unsigned long long)L.renders);
                return false;
            }

            std::fprintf(stderr, "lcd dump|mode [..]|on|off|render|fb <a>|fill <c>|pixel <x> <y> <c>|gradient|checker|bars|plasma [t]|anim [n]|mandel [zoom] [cx] [cy]|life [gens]|screenshot <file>\\n");
            return false;
        }'''

if old not in src:
    print("[!] anchor help-строки в lcd-блоке не найден"); raise SystemExit(1)
src = src.replace(old, new, 1)
p.write_text(src)
print("[+] lcd life добавлен")
