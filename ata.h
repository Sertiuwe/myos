#ifndef ATA_H
#define ATA_H

#include <stdint.h>

typedef struct {
    char     name[8];       /* "ata0", "ata1" */
    char     model[41];     /* до 40 символов + '\0' */
    char     serial[21];    /* до 20 символов + '\0' */
    uint64_t sectors;       /* LBA48 */
    uint32_t reads;
    uint32_t writes;
    uint32_t errors;
    uint64_t bytes_read;
    uint64_t bytes_written;
} ata_info_t;

void ata_init(void);
int  ata_get_info(int idx, ata_info_t *out);
int  ata_count(void);

void ata_dma_enable(void);
void ata_dma_stats(uint32_t *reads, uint32_t *fb);
#endif
