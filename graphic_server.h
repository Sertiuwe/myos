#ifndef GRAPHIC_SERVER_H
#define GRAPHIC_SERVER_H

#include <stdint.h>
#include "graphic_api.h"
#include "sched.h"

#define GS_MAX_WINDOWS 16
#define GS_TITLE_MAX   64

typedef struct gs_window gs_window_t;

typedef enum {
    GS_EV_PAINT = 1, GS_EV_MOUSE_MOVE, GS_EV_MOUSE_DOWN, GS_EV_MOUSE_UP,
    GS_EV_KEY, GS_EV_CLOSE, GS_EV_OPEN, GS_EV_TIMER
} gs_event_type_t;

typedef struct {
    gs_event_type_t type;
    int32_t x, y;      /* client-relative */
    int32_t sx, sy;    /* screen */
    uint8_t buttons;
    char    key;
} gs_event_t;

typedef void (*gs_paint_fn)(gs_window_t *w, void *user);
typedef void (*gs_event_fn)(gs_window_t *w, const gs_event_t *ev, void *user);

struct gs_window {
    ga_rect_t    r;
    char         title[GS_TITLE_MAX];
    gs_paint_fn  paint;
    gs_event_fn  event;
    void        *user;
    int          used;
    int          dirty;
    gs_window_t *next;      /* z-order: head = topmost */
    /* per-window task: events are queued by the compositor and processed by
       this window's own scheduler thread, serialized against paint by lock */
    int          tid;
    kmutex_t     lock;
    gs_event_t   evq[32];
    volatile int ev_head, ev_tail;
    volatile int thread_quit;
    volatile int thread_done;
};

void gs_init(void);
gs_window_t *gs_create(const char *title, ga_rect_t r,
                       gs_paint_fn paint, gs_event_fn event, void *user);
void gs_destroy(gs_window_t *w);
void gs_raise(gs_window_t *w);
void gs_set_title(gs_window_t *w, const char *title);
void gs_invalidate(gs_window_t *w);
void gs_invalidate_all(void);
ga_rect_t gs_client(const gs_window_t *w);
int  gs_active(const gs_window_t *w);
int  gs_window_count(void);
void gs_quit(void);
void gs_run(void);

void gs_pointer(int32_t *sx, int32_t *sy, uint8_t *buttons);
void gs_set_launcher(void (*fn)(void));
void gs_desktop_clear(void);
void gs_desktop_add(const char *label, asset_icon_id_t icon, void (*fn)(void));
void gs_set_options(int grid, int shadow, int clock);
void gs_set_dblclick_ms(uint32_t ms);
uint32_t gs_dblclick_ticks(void);
#endif
