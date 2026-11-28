#include "ga_remote.h"
#include "graphic_server.h"
#include "graphic_api.h"
#include "kstring.h"
#include "kstdio.h"

#define GAR_MAX_WIN 4
#define GAR_CMD_MAX 128
#define GAR_EV_MAX  32

typedef struct {
    int          used;
    gs_window_t *w;
    gar_cmd_t    cmds[GAR_CMD_MAX];
    int          ncmd;
    gar_ev_t     evq[GAR_EV_MAX];
    volatile int ev_head, ev_tail;
} gar_win_t;

static gar_win_t gwin[GAR_MAX_WIN];

static void replay(gar_win_t *g, ga_rect_t cl) {
    for (int i = 0; i < g->ncmd; i++) {
        gar_cmd_t *c = &g->cmds[i];
        ga_rect_t r = { cl.x + c->a, cl.y + c->b, c->c, c->d };
        switch (c->op) {
        case GAR_FILL:  ga_fill(r, c->col); break;
        case GAR_FRAME: ga_frame(r, (uint16_t)c->t, c->col); break;
        case GAR_LINE:  ga_line(cl.x + c->a, cl.y + c->b, cl.x + c->c, cl.y + c->d, c->col); break;
        case GAR_TEXT:  ga_text(cl.x + c->a, cl.y + c->b, c->str, c->col); break;
        case GAR_GRAD:  ga_grad_v(r, c->col, c->col2); break;
        case GAR_CLEAR: g->ncmd = 0; break;
        }
    }
}

static void gar_paint(gs_window_t *w, void *user) {
    gar_win_t *g = (gar_win_t *)user;
    ga_rect_t cl = gs_client(w);
    ga_push_clip(cl);
    replay(g, cl);
    ga_pop_clip();
}

static void gar_event(gs_window_t *w, const gs_event_t *ev, void *user) {
    gar_win_t *g = (gar_win_t *)user;
    (void)w;
    gar_ev_t e;
    e.type = (int32_t)ev->type;
    e.x = ev->x; e.y = ev->y; e.sx = ev->sx; e.sy = ev->sy;
    e.buttons = ev->buttons; e.key = ev->key;
    int nx = (g->ev_tail + 1) % GAR_EV_MAX;
    if (nx != g->ev_head) { g->evq[g->ev_tail] = e; g->ev_tail = nx; }
}

static int valid_handle(int64_t h) { return h >= 0 && h < GAR_MAX_WIN && gwin[h].used; }

int64_t gar_open(const gar_open_t *u) {
    if (!u) return -1;
    int slot = -1;
    for (int i = 0; i < GAR_MAX_WIN; i++) if (!gwin[i].used) { slot = i; break; }
    if (slot < 0) return -1;
    gar_win_t *g = &gwin[slot];
    g->ncmd = 0; g->ev_head = 0; g->ev_tail = 0;
    ga_rect_t r = { u->x, u->y, u->w, u->h };
    g->w = gs_create(u->title, r, gar_paint, gar_event, g);
    if (!g->w) return -1;
    g->used = 1;
    return slot;
}

int64_t gar_cmd(int64_t h, const gar_cmd_t *u) {
    if (!valid_handle(h) || !u) return -1;
    gar_win_t *g = &gwin[h];
    if (g->ncmd >= GAR_CMD_MAX) return -1;
    gar_cmd_t c = *u;
    c.str[GAR_STR_MAX - 1] = '\0';
    g->cmds[g->ncmd++] = c;
    gs_invalidate(g->w);
    return 0;
}

int64_t gar_event_poll(int64_t h, gar_ev_t *u) {
    if (!valid_handle(h) || !u) return -1;
    gar_win_t *g = &gwin[h];
    if (g->ev_head == g->ev_tail) return -1;
    *u = g->evq[g->ev_head];
    g->ev_head = (g->ev_head + 1) % GAR_EV_MAX;
    return 0;
}

int64_t gar_present(int64_t h) {
    if (!valid_handle(h)) return -1;
    gs_invalidate(gwin[h].w);
    return 0;
}

int64_t gar_close(int64_t h) {
    if (!valid_handle(h)) return -1;
    gs_destroy(gwin[h].w);
    gwin[h].used = 0;
    gwin[h].w = NULL;
    return 0;
}
