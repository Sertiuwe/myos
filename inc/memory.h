#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t overhead_bytes;
    uint64_t largest_free;
    uint64_t peak_used;
    uint32_t alloc_calls;
    uint32_t free_calls;
    uint32_t failed_calls;
    uint32_t live_blocks;
    uint32_t free_blocks;
    uint32_t corrupt_events;
} kheap_stats_t;

typedef void (*kheap_report_fn)(const char *s);

void   kheap_set_reporter(kheap_report_fn fn);
void   kheap_report(const char *s);

void   kinit_heap(void);
void  *kmalloc(size_t size);
void  *kcalloc(size_t n, size_t size);
void  *krealloc(void *ptr, size_t new_size);
void  *kmalloc_aligned(size_t alignment, size_t size);
void   kfree(void *ptr);
void  *kstrdup(const char *s);
void  *kmemdup(const void *src, size_t n);
size_t kheap_usable_size(void *ptr);

int    kheap_check(void);
void   kheap_get_stats(kheap_stats_t *out);
void   kheap_dump(void);
void   kheap_dump_around(void *ptr);
void   kheap_selftest(void);

#endif
