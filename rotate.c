int inited;
float angle;
float cx;
float cy;
float rad;
float da;
int vx0; int vy0;
int vx1; int vy1;
int vx2; int vy2;
int vx3; int vy3;
int vx4; int vy4;
int vx5; int vy5;

if (inited == 0) {
    angle = 0.0;
    cx = 160.0;
    cy = 120.0;
    rad = 90.0;
    da = 0.08;
    gfx_init(0x80000);
    inited = 1;
}

gfx_clear();

float a0 = angle;
float a1 = angle + 1.04719755;
float a2 = angle + 2.09439510;
float a3 = angle + 3.14159265;
float a4 = angle + 4.18879020;
float a5 = angle + 5.23598775;

vx0 = (int)(cx + rad * fcos(a0));  vy0 = (int)(cy + rad * fsin(a0));
vx1 = (int)(cx + rad * fcos(a1));  vy1 = (int)(cy + rad * fsin(a1));
vx2 = (int)(cx + rad * fcos(a2));  vy2 = (int)(cy + rad * fsin(a2));
vx3 = (int)(cx + rad * fcos(a3));  vy3 = (int)(cy + rad * fsin(a3));
vx4 = (int)(cx + rad * fcos(a4));  vy4 = (int)(cy + rad * fsin(a4));
vx5 = (int)(cx + rad * fcos(a5));  vy5 = (int)(cy + rad * fsin(a5));

gfx_line(vx0, vy0, vx1, vy1, 0xFFE0);
gfx_line(vx1, vy1, vx2, vy2, 0xFFE0);
gfx_line(vx2, vy2, vx3, vy3, 0xFFE0);
gfx_line(vx3, vy3, vx4, vy4, 0xFFE0);
gfx_line(vx4, vy4, vx5, vy5, 0xFFE0);
gfx_line(vx5, vy5, vx0, vy0, 0xFFE0);
gfx_circle(160, 120, 40, 0xF81F);

angle = angle + da;
if (angle > 6.28318531) angle = angle - 6.28318531;
hlt();
