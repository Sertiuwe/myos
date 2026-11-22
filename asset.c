#include "asset.h"
#include "kstring.h"

static asset_theme_t th;
static asset_metrics_t mt;
static const char *th_name = "dark";

static const asset_theme_t theme_dark = {
    .desktop_bg = 0x101418, .desktop_grid = 0x182028,
    .win_bg = 0x1C2229, .win_border = 0x3A4652,
    .win_title_active = 0x2E6F9E, .win_title_inactive = 0x2A323B,
    .win_title_text = 0xFFFFFF, .win_title_text_dim = 0x9AA7B4,
    .client_bg = 0x141A20,
    .text = 0xDCE3EA, .text_dim = 0x8A97A4, .text_invert = 0x0B0F13,
    .btn_face = 0x2A333D, .btn_hover = 0x35414E, .btn_pressed = 0x1E262E,
    .btn_border = 0x4A5866, .btn_text = 0xE6EDF3,
    .list_bg = 0x121820, .list_alt = 0x171F27,
    .list_sel = 0x2E6F9E, .list_sel_text = 0xFFFFFF,
    .scroll_track = 0x1A222A, .scroll_thumb = 0x44525F,
    .accent = 0x4FA3D1, .danger = 0xD1544F, .ok = 0x5FBF7A,
    .cursor_fill = 0xFFFFFF, .cursor_edge = 0x000000
};

static const asset_theme_t theme_light = {
    .desktop_bg = 0xBFC7CF, .desktop_grid = 0xB2BAC4,
    .win_bg = 0xE9EDF1, .win_border = 0x7C8894,
    .win_title_active = 0x2E6F9E, .win_title_inactive = 0x9AA5B0,
    .win_title_text = 0xFFFFFF, .win_title_text_dim = 0xE1E6EA,
    .client_bg = 0xF5F7F9,
    .text = 0x1A2028, .text_dim = 0x5A6672, .text_invert = 0xFFFFFF,
    .btn_face = 0xDDE3E9, .btn_hover = 0xE9EFF4, .btn_pressed = 0xC7CFD7,
    .btn_border = 0x8B96A2, .btn_text = 0x1A2028,
    .list_bg = 0xFFFFFF, .list_alt = 0xF0F3F6,
    .list_sel = 0x2E6F9E, .list_sel_text = 0xFFFFFF,
    .scroll_track = 0xE3E8ED, .scroll_thumb = 0xA9B3BD,
    .accent = 0x1F6F9E, .danger = 0xB03A34, .ok = 0x2E8B57,
    .cursor_fill = 0xFFFFFF, .cursor_edge = 0x000000
};

static const uint16_t icons[ASSET_ICO_COUNT][ASSET_ICON_SIZE] = {
    [ASSET_ICO_FOLDER] = {
        0x0000,0x0000,0x7C00,0x4200,0x7FFC,0x4004,0x4004,0x4004,
        0x4004,0x4004,0x4004,0x4004,0x7FFC,0x0000,0x0000,0x0000
    },
    [ASSET_ICO_FILE] = {
        0x0000,0x7FFC,0x4004,0x5FF4,0x4004,0x5FF4,0x4004,0x5FF4,
        0x4004,0x5FF4,0x4004,0x5FF4,0x7FFC,0x0000,0x0000,0x0000
    },
    [ASSET_ICO_DISK] = {
        0x0000,0x0000,0x7FFC,0x4004,0x4004,0x4004,0x7FFC,0x4004,
        0x4004,0x5FF4,0x4004,0x7FFC,0x0000,0x0000,0x0000,0x0000
    },
    [ASSET_ICO_UP] = {
        0x0000,0x0180,0x03C0,0x07E0,0x0FF0,0x1FF8,0x3FFC,0x0180,
        0x0180,0x0180,0x0180,0x0180,0x0000,0x0000,0x0000,0x0000
    },
    [ASSET_ICO_CLOSE] = {
        0x0000,0x0000,0x300C,0x1818,0x0C30,0x0660,0x03C0,0x0180,
        0x03C0,0x0660,0x0C30,0x1818,0x300C,0x0000,0x0000,0x0000
    },
    [ASSET_ICO_REFRESH] = {
        0x0000,0x07E0,0x1FF8,0x381C,0x300C,0x303C,0x307C,0x300C,
        0x300C,0x381C,0x1FF8,0x07E0,0x0000,0x0000,0x0000,0x0000
    },
    [ASSET_ICO_TRASH] = {
        0x0000,0x07E0,0x3FFC,0x4004,0x4AA4,0x4AA4,0x4AA4,0x4AA4,
        0x4AA4,0x4AA4,0x4004,0x3FFC,0x0000,0x0000,0x0000,0x0000
    },
    [ASSET_ICO_PLUS] = {
        0x0000,0x0000,0x0000,0x0180,0x0180,0x0180,0x3FFC,0x3FFC,
        0x0180,0x0180,0x0180,0x0000,0x0000,0x0000,0x0000,0x0000
    },
    [ASSET_ICO_SAVE] = {
        0x0000,0x7FFC,0x4004,0x5FF4,0x5FF4,0x4004,0x4004,0x7FFC,
        0x4004,0x4004,0x4004,0x7FFC,0x0000,0x0000,0x0000,0x0000
    },
    [ASSET_ICO_TERMINAL] = {
        0x0000,0x7FFC,0x4004,0x4004,0x4804,0x4404,0x4804,0x4004,
        0x5F04,0x4004,0x7FFC,0x0000,0x0000,0x0000,0x0000,0x0000
    }
};

static const char *icon_names[ASSET_ICO_COUNT] = {
    "folder", "file", "disk", "up", "close",
    "refresh", "trash", "plus", "save", "terminal"
};

static const char *strings[ASSET_STR_COUNT] = {
    [ASSET_STR_DESKTOP]        = "MyOS Desktop",
    [ASSET_STR_APP_FILES]      = "File Manager",
    [ASSET_STR_APP_TERM]       = "Terminal",
    [ASSET_STR_APP_VIEWER]     = "File Viewer",
    [ASSET_STR_BTN_NEW_FOLDER] = "New Folder",
    [ASSET_STR_BTN_NEW_FILE]   = "New File",
    [ASSET_STR_BTN_DELETE]     = "Delete",
    [ASSET_STR_BTN_RENAME]     = "Rename",
    [ASSET_STR_BTN_REFRESH]    = "Refresh",
    [ASSET_STR_BTN_SYNC]       = "Sync",
    [ASSET_STR_BTN_UP]         = "Up",
    [ASSET_STR_BTN_CLOSE]      = "Close",
    [ASSET_STR_BTN_OPEN]       = "Open",
    [ASSET_STR_COL_NAME]       = "Name",
    [ASSET_STR_COL_SIZE]       = "Size",
    [ASSET_STR_COL_MODIFIED]   = "Modified",
    [ASSET_STR_LBL_PATH]       = "Path",
    [ASSET_STR_LBL_EMPTY]      = "(empty)",
    [ASSET_STR_LBL_ITEMS]      = "items",
    [ASSET_STR_MSG_CREATED]    = "created",
    [ASSET_STR_MSG_DELETED]    = "deleted",
    [ASSET_STR_MSG_RENAMED]    = "renamed",
    [ASSET_STR_MSG_SYNCED]     = "synced",
    [ASSET_STR_MSG_FAILED]     = "operation failed",
    [ASSET_STR_MSG_READONLY]   = "read-only"
};

void asset_init(uint16_t font_w, uint16_t font_h) {
    th = theme_dark;
    th_name = "dark";
    mt.title_h    = 22;
    mt.border     = 2;
    mt.btn_h      = 22;
    mt.btn_pad_x  = 8;
    mt.row_h      = 20;
    mt.icon       = ASSET_ICON_SIZE;
    mt.gap        = 6;
    mt.scroll_w   = 12;
    mt.close_size = 14;
    mt.font_w     = font_w ? font_w : 8;
    mt.font_h     = font_h ? font_h : 16;
    if (mt.row_h < (uint16_t)(mt.font_h + 4)) mt.row_h = (uint16_t)(mt.font_h + 4);
    if (mt.title_h < (uint16_t)(mt.font_h + 6)) mt.title_h = (uint16_t)(mt.font_h + 6);
}

const asset_theme_t   *asset_theme(void)   { return &th; }
const asset_metrics_t *asset_metrics(void) { return &mt; }

const uint16_t *asset_icon(asset_icon_id_t id) {
    if (id < 0 || id >= ASSET_ICO_COUNT) return icons[ASSET_ICO_FILE];
    return icons[id];
}

const char *asset_icon_name(asset_icon_id_t id) {
    if (id < 0 || id >= ASSET_ICO_COUNT) return "?";
    return icon_names[id];
}

const char *asset_str(asset_str_id_t id) {
    if (id < 0 || id >= ASSET_STR_COUNT) return "?";
    return strings[id] ? strings[id] : "?";
}

int asset_theme_set(const char *name) {
    if (!name) return -1;
    if (!strcmp(name, "dark"))  { th = theme_dark;  th_name = "dark";  return 0; }
    if (!strcmp(name, "light")) { th = theme_light; th_name = "light"; return 0; }
    return -1;
}

const char *asset_theme_name(void) { return th_name; }
