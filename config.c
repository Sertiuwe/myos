#include "config.h"
#include "syslib.h"
#include "asset.h"
#include "kstring.h"
#include "kstdio.h"



static unsigned parse_u_simple(const char *s) {
    unsigned v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned)(*s - '0'); s++; }
    return v;
}

#define CFG_PATH "/config.conf"

static os_config_t cur;

void config_init_defaults(void) {
    strcpy(cur.theme, "dark");
    cur.grid = 1;
    cur.shadow = 1;
    cur.clock = 1;
    cur.dblclick_ms = 500;
}

os_config_t *config_get(void) { return &cur; }
const char *config_path(void) { return CFG_PATH; }

int config_load(void) {
    int fd = sl_open(CFG_PATH, SL_O_READ);
    if (fd < 0) return -1;
    static char buf[1024];
    int n = sl_read(fd, buf, sizeof(buf) - 1);
    sl_close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';

    config_init_defaults();
    char *p = buf;
    while (p && *p) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = '\0';
        if (p[0] != '#') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                const char *v = eq + 1;
                if (!strcmp(p, "theme")) {
                    strncpy(cur.theme, v, sizeof(cur.theme) - 1);
                    cur.theme[sizeof(cur.theme) - 1] = '\0';
                } else if (!strcmp(p, "grid"))          cur.grid = (v[0] == '1');
                else if (!strcmp(p, "shadow"))         cur.shadow = (v[0] == '1');
                else if (!strcmp(p, "clock"))          cur.clock = (v[0] == '1');
                else if (!strcmp(p, "dblclick_ms"))    cur.dblclick_ms = (uint32_t)parse_u_simple(v);
            }
        }
        if (!eol) break;
        p = eol + 1;
    }
    return 0;
}

int config_save(void) {
    char buf[256];
    ksnprintf(buf, sizeof(buf),
              "# MyOS graphic server configuration\n"
              "theme=%s\ngrid=%d\nshadow=%d\nclock=%d\ndblclick_ms=%u\n",
              cur.theme, cur.grid, cur.shadow, cur.clock,
              (unsigned)cur.dblclick_ms);
    int fd = sl_open(CFG_PATH, SL_O_WRITE | SL_O_CREATE);
    if (fd < 0) return -1;
    int r = sl_write(fd, buf, (uint32_t)strlen(buf));
    sl_close(fd);
    sl_sync();
    return r >= 0 ? 0 : -1;
}

int config_set_theme(const char *name) {
    if (asset_theme_set(name) != 0) return -1;
    strncpy(cur.theme, name, sizeof(cur.theme) - 1);
    cur.theme[sizeof(cur.theme) - 1] = '\0';
    return config_save();
}

int config_set_int(const char *key, int v) {
    if (!strcmp(key, "grid"))             cur.grid = !!v;
    else if (!strcmp(key, "shadow"))      cur.shadow = !!v;
    else if (!strcmp(key, "clock"))       cur.clock = !!v;
    else if (!strcmp(key, "dblclick_ms")) cur.dblclick_ms = (uint32_t)v;
    else return -1;
    return config_save();
}
