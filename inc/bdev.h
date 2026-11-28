#ifndef BDEV_H
#define BDEV_H

#include <stdint.h>

typedef struct bdev {
    const char *name;
    uint64_t sectors;                 /* в 512-байтовых секторах */
    int (*read)(struct bdev *, uint64_t lba, uint32_t count, void *buf);
    int (*write)(struct bdev *, uint64_t lba, uint32_t count, const void *buf);
    void *priv;
} bdev_t;

int      bdev_register(bdev_t *b);
bdev_t  *bdev_by_name(const char *name);
bdev_t  *bdev_by_index(int i);
int      bdev_count(void);
int      bdev_read(bdev_t *b, uint64_t lba, uint32_t count, void *buf);
int      bdev_write(bdev_t *b, uint64_t lba, uint32_t count, const void *buf);

#endif
