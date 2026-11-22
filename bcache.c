#include "bcache.h"
#include "memory.h"
#include "kstring.h"
#include <stdint.h>

#define NBUCKETS 64
#define BLKSIZE  512

typedef struct buf {
    struct buf  *next;      /* хэш-цепочка */
    bdev_t      *dev;
    uint64_t     lba;
    uint32_t     refcnt;
    uint8_t      dirty;
    uint8_t      valid;
    uint8_t      data[BLKSIZE];
} buf_t;

static buf_t *buckets[NBUCKETS];
static uint32_t st_hits, st_misses, st_dirty, st_total;

static inline unsigned hashof(bdev_t *b, uint64_t lba) {
    return (unsigned)(((uintptr_t)b ^ lba) % NBUCKETS);
}

static buf_t *lookup(bdev_t *b, uint64_t lba) {
    unsigned h = hashof(b, lba);
    for (buf_t *bp = buckets[h]; bp; bp = bp->next)
        if (bp->dev == b && bp->lba == lba) return bp;
    return NULL;
}

static buf_t *alloc_buf(bdev_t *b, uint64_t lba) {
    /* Простая стратегия: если бакет полон (>8), вытесняем чистый LRU-подобный.
       Для начала — просто создаём новый, eviction добавим при необходимости. */
    buf_t *bp = (buf_t *)kmalloc(sizeof(buf_t));
    if (!bp) return NULL;
    memset(bp, 0, sizeof(*bp));
    bp->dev = b;
    bp->lba = lba;
    bp->refcnt = 1;
    unsigned h = hashof(b, lba);
    bp->next = buckets[h];
    buckets[h] = bp;
    st_total++;
    return bp;
}

void bcache_init(void) {
    memset(buckets, 0, sizeof buckets);
    st_hits = st_misses = st_dirty = st_total = 0;
}

int bcache_read(bdev_t *b, uint64_t lba, void *buf) {
    buf_t *bp = lookup(b, lba);
    if (bp && bp->valid) {
        memcpy(buf, bp->data, BLKSIZE);
        st_hits++;
        return 0;
    }
    /* Miss: читаем с диска */
    if (!bp) bp = alloc_buf(b, lba);
    if (!bp) return -1;
    int rc = bdev_read(b, lba, 1, bp->data);
    if (rc) { kfree(bp); return rc; }
    bp->valid = 1;
    memcpy(buf, bp->data, BLKSIZE);
    st_misses++;
    return 0;
}

int bcache_write(bdev_t *b, uint64_t lba, const void *buf) {
    buf_t *bp = lookup(b, lba);
    if (!bp) bp = alloc_buf(b, lba);
    if (!bp) return -1;
    memcpy(bp->data, buf, BLKSIZE);
    bp->valid = 1;
    bp->dirty = 1;
    if (!st_dirty) st_dirty = 1; /* упрощённый счётчик */
    /* Write-through для надёжности: пишем сразу + помечаем dirty для будущего writeback */
    int rc = bdev_write(b, lba, 1, buf);
    if (rc) return rc;
    bp->dirty = 0;
    return 0;
}

int bcache_flush(bdev_t *b) {
    /* Writeback: сбрасываем все dirty буферы для данного устройства */
    for (unsigned h = 0; h < NBUCKETS; h++) {
        for (buf_t *bp = buckets[h]; bp; bp = bp->next) {
            if (bp->dev == b && bp->dirty && bp->valid) {
                int rc = bdev_write(b, bp->lba, 1, bp->data);
                if (rc) return rc;
                bp->dirty = 0;
            }
        }
    }
    return 0;
}

void bcache_stats(uint32_t *hits, uint32_t *misses, uint32_t *dirty, uint32_t *total) {
    if (hits) *hits = st_hits;
    if (misses) *misses = st_misses;
    if (dirty) *dirty = st_dirty;
    if (total) *total = st_total;
}

void bcache_invalidate(bdev_t *b) {
    for (unsigned h = 0; h < NBUCKETS; h++) {
        buf_t **pp = &buckets[h];
        while (*pp) {
            buf_t *bp = *pp;
            if (bp->dev == b) {
                *pp = bp->next;
                kfree(bp);
                st_total--;
            } else {
                pp = &bp->next;
            }
        }
    }
}
