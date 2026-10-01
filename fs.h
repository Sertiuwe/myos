#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_MAGIC         0x4D594653u
#define FS_BLOCK_SIZE    512
#define FS_TOTAL_BLOCKS  1024        /* 512 KB data area */
#define FS_MAX_ENTRIES   128
#define FS_MAX_NAME      32
#define FS_MAX_PATH      256
#define FS_ROOT          0

#define FT_FREE          0
#define FT_FILE          1
#define FT_DIR           2

#define FS_EOF           0xFFFF
#define FS_FREE_BLOCK    0x0000

typedef struct {
    char     name[FS_MAX_NAME];
    uint32_t size;
    uint16_t first_block;
    uint8_t  type;
    uint8_t  parent;
    uint8_t  used;
    uint8_t  reserved;
    uint32_t mtime;
} fs_entry_t;

void fs_init(void);
void fs_format(void);
int  fs_load_from_disk(void);
int  fs_sync_to_disk(void);

uint32_t fs_total_blocks(void);
uint32_t fs_free_blocks(void);
uint32_t fs_total_entries(void);
uint32_t fs_free_entries(void);

int  fs_find(uint8_t parent, const char *name);
int  fs_resolve(const char *path);
int  fs_resolve_rel(const char *path, uint8_t cwd);

int  fs_mkdir(uint8_t parent, const char *name);
int  fs_create(uint8_t parent, const char *name);

int  fs_delete(int idx);
int  fs_write(int idx, const uint8_t *buf, uint32_t len);
int  fs_read(int idx, uint8_t *buf, uint32_t offset, uint32_t len);

void fs_path(int idx, char *buf, int bufsize);
const char *fs_name(int idx);
uint8_t fs_type(int idx);
uint32_t fs_size(int idx);

int  fs_list(uint8_t parent, int *out, int max);

#endif
