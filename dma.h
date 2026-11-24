#ifndef DMA_H
#define DMA_H
#include <stdint.h>
#include <stddef.h>
void    *dma_alloc(size_t size, uint64_t *phys_out);
void     dma_free(void *ptr);
uint64_t dma_virt_to_phys(const void *va);
int      dma_selftest(void);
#endif
