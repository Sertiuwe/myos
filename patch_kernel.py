with open("kernel.c") as f:
    src = f.read()

# Include sdcard.h
if '#include "sdcard.h"' not in src:
    src = src.replace('#include "fs.h"', '#include "fs.h"\n#include "sdcard.h"')
    print("[1] include sdcard.h")

# Расширенный boot-баннер
old = '''    puts("Initializing MyFS...\\n");
    fs_init();
    puts("  filesystem ready\\n");'''

new = '''    puts("Initializing SD card...\\n");
    if (sd_present()) {
        puts("  card present, blocks = ");
        putuint(sd_total_blocks());
        puts("\\n");
    } else {
        puts("  no card (RAM-only mode)\\n");
    }

    puts("Initializing MyFS...\\n");
    fs_init();
    puts("  filesystem ready\\n");'''

if old in src:
    src = src.replace(old, new, 1)
    print("[2] boot banner расширен")
else:
    print("[!] не нашёл 'Initializing MyFS'")

# Команды sync и sd
old_df = '''    else if (streq(cmd, "df")) {'''
new_df = '''    else if (streq(cmd, "sync")) {
        if (!sd_present()) { puts("no SD card\\n"); }
        else if (fs_sync_to_disk() == 0) puts("synced to SD\\n");
        else puts("sync FAILED\\n");
    }
    else if (streq(cmd, "sd")) {
        puts("SD card status:\\n");
        if (sd_present()) {
            puts("  present = yes\\n");
            puts("  size    = ");
            putuint(sd_total_blocks());
            puts(" blocks (");
            putuint(sd_total_blocks() / 2);
            puts(" KB)\\n");
        } else {
            puts("  present = no\\n");
        }
    }
    else if (streq(cmd, "df")) {'''

if old_df in src:
    src = src.replace(old_df, new_df, 1)
    print("[3] команды sync/sd добавлены")
else:
    print("[!] не нашёл df")

with open("kernel.c", "w") as f:
    f.write(src)
