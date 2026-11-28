#include "myos_app.h"
#include "astudio.h"

MYOS_APP_META("paint", 32768);

#define MAX_SEG 100
typedef struct { int32_t x0,y0,x1,y1; uint32_t col; } seg_t;
static seg_t seg[MAX_SEG];
static int nseg;
static uint32_t pen = 0x66ccff;
static int have_last, lx, ly;

static void redraw(int h) {
    as_ga_clear(h);
    as_ga_grad(h, 0, 0, 420, 260, 0x1b1f27, 0x232a35);
    as_ga_frame(h, 0, 0, 420, 260, 1, 0x3a4359);
    as_ga_text(h, 8, 6, "astudio paint  [drag]=draw  c=clear", 0x9fb4d8);
    for (int i = 0; i < nseg; i++)
        as_ga_line(h, seg[i].x0, seg[i].y0+20, seg[i].x1, seg[i].y1+20, seg[i].col);
    as_ga_present(h);
}

void _start(void) {
    as_printf("[paint] start, tid=%d\n", as_tid());
    int h = as_ga_open("astudio paint", 220, 140, 420, 300);
    as_printf("[paint] open h=%d\n", h);
    if (h < 0) { as_exit(1); for(;;){} }
    redraw(h);

    for (;;) {
        gar_ev_t ev;
        while (as_ga_event(h, &ev) == 0) {
            if (ev.type == GAR_EV_CLOSE) { as_ga_close(h); as_exit(0); for(;;){} }
            else if (ev.type == GAR_EV_KEY && (ev.key=='c'||ev.key=='C')) { nseg=0; have_last=0; redraw(h); }
            else if (ev.type == GAR_EV_MOUSE_DOWN) { have_last=1; lx=ev.x; ly=ev.y; }
            else if (ev.type == GAR_EV_MOUSE_UP) { have_last=0; }
            else if (ev.type == GAR_EV_MOUSE_MOVE && (ev.buttons & 1)) {
                if (have_last && nseg < MAX_SEG) {
                    seg[nseg].x0=lx; seg[nseg].y0=ly; seg[nseg].x1=ev.x; seg[nseg].y1=ev.y; seg[nseg].col=pen;
                    nseg++;
                }
                lx=ev.x; ly=ev.y; have_last=1;
                redraw(h);
            }
        }
        /* throttle ~60 fps using uptime (no extra imports) */
        uint64_t next = as_uptime_ms() + 16;
        while (as_uptime_ms() < next) { }
    }
}
