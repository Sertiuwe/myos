with open("fs.c") as f:
    src = f.read()

# 1. Делаем fs_entries volatile — гарантирует что каждая запись идёт в память
old = 'static fs_entry_t fs_entries[FS_MAX_ENTRIES] __attribute__((used, aligned(8)));'
new = 'static volatile fs_entry_t fs_entries[FS_MAX_ENTRIES] __attribute__((used, aligned(8)));'
if old in src:
    src = src.replace(old, new, 1)
    print("[1] fs_entries volatile")
else:
    print("[!] не нашёл объявление fs_entries")

# 2. fs_format — полностью переписываем с явным volatile и read-back
import re
pattern = r'void fs_format\(void\) \{.*?\n\}'
new_format = '''void fs_format(void) {
    printf("[FS] format start\\n");

    /* Обнуляем FAT через volatile */
    volatile uint16_t *vat = fs_fat;
    for (uint32_t i = 0; i < FS_TOTAL_BLOCKS; i++) vat[i] = FS_FREE_BLOCK;

    /* Обнуляем ВСЕ записи через volatile */
    volatile uint8_t *raw = (volatile uint8_t*)fs_entries;
    for (uint32_t i = 0; i < sizeof(fs_entries); i++) raw[i] = 0;

    /* Настраиваем корень через volatile */
    volatile fs_entry_t *r = &fs_entries[FS_ROOT];
    r->name[0] = '/';
    r->name[1] = 0;
    r->type    = FT_DIR;
    r->parent  = 0xFF;
    r->size    = 0;
    r->used    = 1;
    r->first_block = FS_EOF;

    /* READ-BACK для верификации */
    printf("[FS] root verify: name[0]=%u type=%u used=%u\\n",
           (unsigned)r->name[0], (unsigned)r->type, (unsigned)r->used);

    /* Создаём стандартные записи */
    int etc = fs_mkdir(FS_ROOT, "etc");
    printf("[FS] mkdir /etc = %d\\n", etc);
    if (etc >= 0) {
        int f = fs_create((uint8_t)etc, "motd");
        if (f >= 0) {
            const char *msg = "Welcome to MyOS!\\nThis is MyFS.\\n";
            fs_write(f, (const uint8_t*)msg, m_strlen(msg));
        }
        int f2 = fs_create((uint8_t)etc, "version");
        if (f2 >= 0) {
            const char *msg = "MyOS v0.4 with MyFS\\n";
            fs_write(f2, (const uint8_t*)msg, m_strlen(msg));
        }
    }

    int home = fs_mkdir(FS_ROOT, "home");
    printf("[FS] mkdir /home = %d\\n", home);
    if (home >= 0) {
        fs_mkdir((uint8_t)home, "user");
    }

    printf("[FS] format done, free_entries=%u\\n", fs_free_entries());
}'''

src_new, n = re.subn(pattern, new_format, src, count=1, flags=re.DOTALL)
if n > 0:
    src = src_new
    print("[2] fs_format перезаписан")
else:
    print("[!] не нашёл fs_format")

with open("fs.c", "w") as f:
    f.write(src)
