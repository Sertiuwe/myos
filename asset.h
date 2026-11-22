#ifndef ASSET_H
#define ASSET_H

#include <stdint.h>
#include <stddef.h>

#define ASSET_ICON_SIZE 16

typedef struct {
    uint32_t desktop_bg, desktop_grid;
    uint32_t win_bg, win_border;
    uint32_t win_title_active, win_title_inactive;
    uint32_t win_title_text, win_title_text_dim;
    uint32_t client_bg;
    uint32_t text, text_dim, text_invert;
    uint32_t btn_face, btn_hover, btn_pressed, btn_border, btn_text;
    uint32_t list_bg, list_alt, list_sel, list_sel_text;
    uint32_t scroll_track, scroll_thumb;
    uint32_t accent, danger, ok;
    uint32_t cursor_fill, cursor_edge;
} asset_theme_t;

typedef struct {
    uint16_t title_h, border;
    uint16_t btn_h, btn_pad_x;
    uint16_t row_h, icon, gap;
    uint16_t scroll_w, close_size;
    uint16_t font_w, font_h;
} asset_metrics_t;

typedef enum {
    ASSET_ICO_FOLDER = 0, ASSET_ICO_FILE, ASSET_ICO_DISK, ASSET_ICO_UP,
    ASSET_ICO_CLOSE, ASSET_ICO_REFRESH, ASSET_ICO_TRASH, ASSET_ICO_PLUS,
    ASSET_ICO_SAVE, ASSET_ICO_TERMINAL,
    ASSET_ICO_COUNT
} asset_icon_id_t;

typedef enum {
    ASSET_STR_DESKTOP = 0,
    ASSET_STR_APP_FILES, ASSET_STR_APP_TERM, ASSET_STR_APP_VIEWER,
    ASSET_STR_BTN_NEW_FOLDER, ASSET_STR_BTN_NEW_FILE, ASSET_STR_BTN_DELETE,
    ASSET_STR_BTN_RENAME, ASSET_STR_BTN_REFRESH, ASSET_STR_BTN_SYNC,
    ASSET_STR_BTN_UP, ASSET_STR_BTN_CLOSE, ASSET_STR_BTN_OPEN,
    ASSET_STR_COL_NAME, ASSET_STR_COL_SIZE, ASSET_STR_COL_MODIFIED,
    ASSET_STR_LBL_PATH, ASSET_STR_LBL_EMPTY, ASSET_STR_LBL_ITEMS,
    ASSET_STR_MSG_CREATED, ASSET_STR_MSG_DELETED, ASSET_STR_MSG_RENAMED,
    ASSET_STR_MSG_SYNCED, ASSET_STR_MSG_FAILED, ASSET_STR_MSG_READONLY,
    ASSET_STR_COUNT
} asset_str_id_t;

void asset_init(uint16_t font_w, uint16_t font_h);
const asset_theme_t   *asset_theme(void);
const asset_metrics_t *asset_metrics(void);
const uint16_t *asset_icon(asset_icon_id_t id);      /* 16 rows, 1bpp, MSB = left */
const char *asset_icon_name(asset_icon_id_t id);
const char *asset_str(asset_str_id_t id);
int  asset_theme_set(const char *name);              /* "dark" | "light" */
const char *asset_theme_name(void);

#endif
