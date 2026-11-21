#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#define NULL ((void*)0)

void kinit_heap(void);
void* kmalloc(uint64_t size);
void kfree(void* ptr);

#endif
