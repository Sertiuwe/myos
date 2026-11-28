#ifndef OSDISK_H
#define OSDISK_H
#include <stdint.h>
void osdisk_info(void);
void osdisk_hex(uint32_t lba, uint32_t nsec);
void osdisk_cmd(const char *arg);
#endif
