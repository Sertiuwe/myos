#include "bdev.h"
#include "kstring.h"

#define MAX_BDEV 4
static bdev_t *devs[MAX_BDEV];
static int ndev;

int bdev_register(bdev_t *b) {
    if (ndev >= MAX_BDEV || !b) return -1;
    devs[ndev++] = b;
    return 0;
}
bdev_t *bdev_by_name(const char *name) {
    for (int i = 0; i < ndev; i++)
        if (strcmp(devs[i]->name, name) == 0) return devs[i];
    return NULL;
}
bdev_t *bdev_by_index(int i) { return (i >= 0 && i < ndev) ? devs[i] : NULL; }
int bdev_count(void) { return ndev; }

int bdev_read(bdev_t *b, uint64_t lba, uint32_t count, void *buf) {
    if (!b || !b->read || !count) return -1;
    if (lba + count > b->sectors) return -1;
    return b->read(b, lba, count, buf);
}
int bdev_write(bdev_t *b, uint64_t lba, uint32_t count, const void *buf) {
    if (!b || !b->write || !count) return -1;
    if (lba + count > b->sectors) return -1;
    return b->write(b, lba, count, buf);
}
