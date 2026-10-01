#include "sdcard.h"

int sd_present(void) {
    return (SD_STATUS & 1) != 0;
}

uint32_t sd_total_blocks(void) {
    return SD_SIZE;
}

int sd_read_block(uint32_t lba, void *buf) {
    if (!sd_present()) return -1;
    SD_LBA = lba;
    SD_CMD = SD_CMD_READ;         /* 1. команда → эмулятор загрузил block_buf */
    if (SD_ERROR) return -2;
    SD_OFFSET = 0;                /* 2. сброс offset */
    uint32_t *p = (uint32_t*)buf;
    for (int i = 0; i < 128; i++) p[i] = SD_DATA;   /* 3. читаем 512 байт */
    return 0;
}

int sd_write_block(uint32_t lba, const void *buf) {
    if (!sd_present()) return -1;
    SD_LBA = lba;
    SD_OFFSET = 0;                /* 1. offset в 0 */
    const uint32_t *p = (const uint32_t*)buf;
    for (int i = 0; i < 128; i++) SD_DATA = p[i];   /* 2. заполняем block_buf */
    SD_CMD = SD_CMD_WRITE;        /* 3. команда → эмулятор пишет block_buf на диск */
    if (SD_ERROR) return -2;
    return 0;
}

void sd_flush(void) {
    SD_CMD = SD_CMD_FLUSH;
}
