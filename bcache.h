#ifndef BCACHE_H
#define BCACHE_H
#include "bdev.h"
void bcache_init(void);
int  bcache_read(bdev_t *b, uint64_t lba, void *buf);
int  bcache_write(bdev_t *b, uint64_t lba, const void *buf);
int  bcache_flush(bdev_t *b);
void bcache_stats(uint32_t *hits, uint32_t *misses, uint32_t *dirty, uint32_t *total);
void bcache_invalidate(bdev_t *b);
#endif
