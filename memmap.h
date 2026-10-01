#ifndef MEMMAP_H
#define MEMMAP_H

#include "types.h"

#define MEM_TYPE_KERNEL    0
#define MEM_TYPE_HEAP      1
#define MEM_TYPE_STACK     2
#define MEM_TYPE_MMIO      3
#define MEM_TYPE_FREE      4

typedef struct {
    const char *name;
    uint32_t    start;
    uint32_t    end;
    int         type;
} mem_region_t;

void memmap_init(void);
const mem_region_t *memmap_get(int *count);
void memmap_dump(void);

/* Аллокатор (bump allocator, без free) */
void    *kmalloc(uint32_t size);
void     kfree(void *p);
uint32_t heap_used(void);
uint32_t heap_free(void);
uint32_t heap_size(void);
void    *heap_start(void);
void    *heap_end(void);

#endif
