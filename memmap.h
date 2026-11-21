#ifndef MEMMAP_H
#define MEMMAP_H

#include <stdint.h>

typedef struct {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t acpi;
} e820_entry_t;

int  memmap_load(uint16_t count);
void memmap_free(void);
uint64_t memmap_usable(void);
uint32_t memmap_count(void);
const e820_entry_t *memmap_entries(void);
void memmap_dump(void);

#endif
