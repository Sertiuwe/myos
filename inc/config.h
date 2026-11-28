#ifndef CONFIG_H
#define CONFIG_H
#include <stdint.h>

typedef struct {
    char     theme[16];
    int      grid;
    int      shadow;
    int      clock;
    uint32_t dblclick_ms;
} os_config_t;

void         config_init_defaults(void);
int          config_load(void);
int          config_save(void);
os_config_t *config_get(void);
int          config_set_theme(const char *name);
int          config_set_int(const char *key, int v);
const char  *config_path(void);

#endif
