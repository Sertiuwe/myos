#ifndef SDCARD_H
#define SDCARD_H

#include "types.h"

#define SD_BASE 0x10100000u
#define SD_REG(off) (*(volatile uint32_t*)(SD_BASE + (off)))

#define SD_STATUS   SD_REG(0x00)
#define SD_CMD      SD_REG(0x04)
#define SD_LBA      SD_REG(0x08)
#define SD_COUNT    SD_REG(0x0C)
#define SD_DATA     SD_REG(0x10)
#define SD_OFFSET   SD_REG(0x14)
#define SD_SIZE     SD_REG(0x18)
#define SD_ERROR    SD_REG(0x1C)

#define SD_CMD_READ  1
#define SD_CMD_WRITE 2
#define SD_CMD_FLUSH 3

int      sd_present(void);
uint32_t sd_total_blocks(void);
int      sd_read_block(uint32_t lba, void *buf);
int      sd_write_block(uint32_t lba, const void *buf);
void     sd_flush(void);

#endif
