#ifndef GA_REMOTE_H
#define GA_REMOTE_H
#include <stdint.h>

/* shared contract between kernel (ga_remote.c) and Ring 3 apps (astudio) */
enum {
    GAR_FILL = 1, GAR_FRAME, GAR_LINE, GAR_TEXT, GAR_GRAD, GAR_CLEAR
};

enum {
    GAR_EV_PAINT = 1, GAR_EV_MOUSE_MOVE, GAR_EV_MOUSE_DOWN, GAR_EV_MOUSE_UP,
    GAR_EV_KEY, GAR_EV_CLOSE, GAR_EV_OPEN, GAR_EV_TIMER
};

#define GAR_STR_MAX 48

typedef struct {
    uint32_t op;
    int32_t  a, b, c, d;      /* rect or line endpoints / text pos */
    uint32_t col, col2;       /* fill/line/text color, grad top/bottom */
    uint32_t t;               /* frame thickness */
    char     str[GAR_STR_MAX];
} gar_cmd_t;

typedef struct {
    char    title[32];
    int32_t x, y, w, h;
} gar_open_t;

typedef struct {
    int32_t  type;
    int32_t  x, y;            /* client-relative */
    int32_t  sx, sy;          /* screen */
    uint32_t buttons;
    char     key;
} gar_ev_t;
int64_t gar_open(const gar_open_t *u);
int64_t gar_cmd(int64_t h, const gar_cmd_t *u);
int64_t gar_event_poll(int64_t h, gar_ev_t *u);
int64_t gar_present(int64_t h);
int64_t gar_close(int64_t h);
#endif
