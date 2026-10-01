// EP32cm demo: несколько цветных прямоугольников и линий
int i;
int color;

gfx_init(0x80000);
gfx_clear();

// Красный квадрат
color = color565(31, 0, 0);
gfx_rect(20, 20, 100, 100, color);

// Зелёный
color = color565(0, 63, 0);
gfx_rect(120, 20, 200, 100, color);

// Синий
color = color565(0, 0, 31);
gfx_rect(220, 20, 300, 100, color);

// Жёлтая горизонтальная линия
color = color565(31, 63, 0);
gfx_hline(20, 300, 130, color);

// Белая вертикальная
color = color565(31, 63, 31);
gfx_vline(160, 150, 230, color);

hlt();
