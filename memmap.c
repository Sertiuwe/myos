#include "memmap.h"
#include "memory.h"
#include "con.h"
#include "kstring.h"

#define E820_SRC ((const uint8_t *)0x9000)
#define E820_STRIDE 24

static e820_entry_t *map;
static uint32_t cnt;
static uint64_t usable;

int memmap_load(uint16_t count) {
    memmap_free();
    cnt = count;
    if (!cnt) return -1;
    map = (e820_entry_t *)kmalloc((size_t)cnt * sizeof(e820_entry_t));
    if (!map) { cnt = 0; return -1; }
    usable = 0;
    const uint8_t *p = E820_SRC;
    for (uint32_t i = 0; i < cnt; i++) {
        uint64_t base, len; uint32_t type, acpi;
        memcpy(&base, p, 8);
        memcpy(&len, p + 8, 8);
        memcpy(&type, p + 16, 4);
        memcpy(&acpi, p + 20, 4);
        map[i].base = base; map[i].len = len;
        map[i].type = type; map[i].acpi = acpi;
        if (type == 1) usable += len;
        p += E820_STRIDE;
    }
    return 0;
}

void memmap_free(void) {
    if (map) { kfree(map); map = NULL; }
    cnt = 0; usable = 0;
}

uint64_t memmap_usable(void) { return usable; }
uint32_t memmap_count(void) { return cnt; }
const e820_entry_t *memmap_entries(void) { return map; }

void memmap_dump(void) {
    con_puts("\n--- E820 MEMORY MAP (in heap) ---\n");
    con_puts("entries = "); con_put_u(cnt);
    con_puts("  usable = "); con_put_u(usable / (1024 * 1024)); con_puts(" MiB\n");
    for (uint32_t i = 0; i < cnt; i++) {
        con_puts("  ["); con_put_u(i); con_puts("] base=");
        con_put_x(map[i].base); con_puts(" len=");
        con_put_x(map[i].len); con_puts(" type=");
        con_put_u(map[i].type);
        con_puts(map[i].type == 1 ? "  USABLE\n" : "\n");
    }
}
