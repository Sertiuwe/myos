#ifndef DMA_H
#define DMA_H
#include <stdint.h>
#include <stddef.h>
void    *dma_alloc(size_t size, uint64_t *phys_out);
void     dma_free(void *ptr);
uint64_t dma_virt_to_phys(const void *va);
int      dma_selftest(void);
int      dma_ata_bm_init(uint16_t *bmiba_out);
void     dma_ata_bm_dump(void);
int      dma_ata_read(int slave, uint32_t lba, uint8_t nsec, void *buf);
int      dma_bench(void);
#endif
