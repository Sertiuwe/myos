/* DVD bouncer без функций — всё inline, глобалы сохраняют состояние */

int inited;
float x;
float y;
float vx;
float vy;
int xi;
int yi;

if (inited == 0) {
    x = 120.0;
    y = 100.0;
    vx = 2.5;
    vy = 1.8;
    gfx_init(0x80000);
    inited = 1;
}

gfx_clear();

xi = (int)x;
yi = (int)y;

/* Внешняя жёлтая рамка */
gfx_rect(xi,   yi,   xi+64, yi+32, 0xFFE0);
/* Внутренняя чёрная */
gfx_rect(xi+3, yi+3, xi+61, yi+29, 0x0000);

/* "D" */
gfx_rect(xi+6,  yi+8,  xi+10, yi+24, 0xFFE0);
gfx_rect(xi+10, yi+8,  xi+18, yi+12, 0xFFE0);
gfx_rect(xi+10, yi+20, xi+18, yi+24, 0xFFE0);

/* "V" */
gfx_rect(xi+22, yi+8,  xi+26, yi+18, 0xFFE0);
gfx_rect(xi+26, yi+18, xi+30, yi+24, 0xFFE0);
gfx_rect(xi+30, yi+18, xi+34, yi+24, 0xFFE0);
gfx_rect(xi+34, yi+8,  xi+38, yi+18, 0xFFE0);

/* "D" вторая */
gfx_rect(xi+42, yi+8,  xi+46, yi+24, 0xFFE0);
gfx_rect(xi+46, yi+8,  xi+54, yi+12, 0xFFE0);
gfx_rect(xi+46, yi+20, xi+54, yi+24, 0xFFE0);

/* update position */
x = x + vx;
y = y + vy;
if (x < 0.0)    { x = 0.0;    vx = -vx; }
if (x > 256.0)  { x = 256.0;  vx = -vx; }
if (y < 0.0)    { y = 0.0;    vy = -vy; }
if (y > 208.0)  { y = 208.0;  vy = -vy; }

hlt();
