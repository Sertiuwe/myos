with open("fs.c") as f:
    src = f.read()

if '#include "sdcard.h"' not in src:
    src = src.replace('#include "match.h"',
                      '#include "match.h"\n#include "sdcard.h"')
    print("[1] include sdcard.h")

old_init = '''void fs_init(void) {
    fs_format();
}'''

new_init = '''int fs_load_from_disk(void) {
    uint8_t sb[512];
    if (sd_read_block(0, sb) != 0) return -1;
    uint32_t magic = *(uint32_t*)sb;
    if (magic != FS_MAGIC) return -1;

    for (uint32_t i = 0; i < FS_TOTAL_BLOCKS; i++) {
        if (sd_read_block(1 + i, &fs_data[i * FS_BLOCK_SIZE]) != 0) return -2;
    }
    uint8_t *fatp = (uint8_t*)fs_fat;
    for (uint32_t i = 0; i < 4; i++) {
        if (sd_read_block(1025 + i, fatp + i * 512) != 0) return -3;
    }
    uint8_t *entp = (uint8_t*)fs_entries;
    for (uint32_t i = 0; i < 12; i++) {
        if (sd_read_block(1029 + i, entp + i * 512) != 0) return -4;
    }
    return 0;
}

int fs_sync_to_disk(void) {
    uint8_t sb[512];
    for (int i = 0; i < 512; i++) sb[i] = 0;
    *(uint32_t*)sb = FS_MAGIC;
    *(uint32_t*)(sb + 4) = 1;
    if (sd_write_block(0, sb) != 0) return -1;

    for (uint32_t i = 0; i < FS_TOTAL_BLOCKS; i++) {
        if (sd_write_block(1 + i, &fs_data[i * FS_BLOCK_SIZE]) != 0) return -2;
    }
    uint8_t *fatp = (uint8_t*)fs_fat;
    for (uint32_t i = 0; i < 4; i++) {
        if (sd_write_block(1025 + i, fatp + i * 512) != 0) return -3;
    }
    uint8_t *entp = (uint8_t*)fs_entries;
    for (uint32_t i = 0; i < 12; i++) {
        if (sd_write_block(1029 + i, entp + i * 512) != 0) return -4;
    }
    sd_flush();
    return 0;
}

void fs_init(void) {
    if (!sd_present()) {
        fs_format();
        return;
    }
    if (fs_load_from_disk() != 0) {
        fs_format();
        fs_sync_to_disk();
    }
}'''

if old_init in src:
    src = src.replace(old_init, new_init, 1)
    print("[2] fs_init заменён")
else:
    print("[!] не нашёл old fs_init")

with open("fs.c", "w") as f:
    f.write(src)
