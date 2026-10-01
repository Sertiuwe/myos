/* ============================================================
 * fs.c — MyFS: FAT-подобная файловая система в памяти
 *   * 512-байтные блоки, FAT-таблица для цепочек
 *   * Иерархия директорий через parent index
 *   * Формат: 512 KB данных + 2 KB FAT + 6 KB entries
 * ============================================================ */
#include "fs.h"
#include "match.h"
#include "sdcard.h"
#include "print.h"

static uint8_t    fs_data[FS_TOTAL_BLOCKS * FS_BLOCK_SIZE] __attribute__((used, aligned(8)));
static uint16_t   fs_fat[FS_TOTAL_BLOCKS] __attribute__((used, aligned(8)));
static fs_entry_t fs_entries[FS_MAX_ENTRIES] __attribute__((used, aligned(8)));

/* ============================================================
 * Низкоуровневые
 * ============================================================ */
static void fs_block_zero(uint16_t b) {
    uint8_t *p = &fs_data[(uint32_t)b * FS_BLOCK_SIZE];
    for (uint32_t i = 0; i < FS_BLOCK_SIZE; i++) p[i] = 0;
}

static int fs_alloc_block(void) {
    for (uint32_t i = 0; i < FS_TOTAL_BLOCKS; i++) {
        if (fs_fat[i] == FS_FREE_BLOCK) {
            fs_fat[i] = FS_EOF;
            fs_block_zero((uint16_t)i);
            return (int)i;
        }
    }
    return -1;
}

static void fs_free_chain(uint16_t start) {
    uint16_t b = start;
    int guard = 0;
    while (b != FS_EOF && b != FS_FREE_BLOCK && b < FS_TOTAL_BLOCKS && guard++ < FS_TOTAL_BLOCKS) {
        uint16_t next = fs_fat[b];
        fs_fat[b] = FS_FREE_BLOCK;
        b = next;
    }
}

static int fs_find_free_entry(void) {
    for (int i = 1; i < FS_MAX_ENTRIES; i++) {
        /* Проверяем name[0] == 0 — надёжнее чем used, которая может
         * быть не выставлена из-за оптимизаций компилятора */
        if (fs_entries[i].name[0] == 0) return i;
    }
    return -1;
}

/* ============================================================
 * Init / Format
 * ============================================================ */
void fs_format(void) {
    printf("[FS] format start\n");

    for (uint32_t i = 0; i < FS_TOTAL_BLOCKS; i++) fs_fat[i] = FS_FREE_BLOCK;

    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        fs_entries[i].name[0] = 0;
        fs_entries[i].used = 0;
        fs_entries[i].type = 0;
        fs_entries[i].parent = 0;
        fs_entries[i].size = 0;
        fs_entries[i].first_block = FS_EOF;
    }

    fs_entries[FS_ROOT].used = 1;
    fs_entries[FS_ROOT].type = FT_DIR;
    fs_entries[FS_ROOT].parent = 0xFF;
    fs_entries[FS_ROOT].size = 0;
    fs_entries[FS_ROOT].first_block = FS_EOF;
    fs_entries[FS_ROOT].name[0] = '/';
    fs_entries[FS_ROOT].name[1] = 0;

    printf("[FS] root verify: name[0]=%u type=%u used=%u\n",
           (unsigned)fs_entries[FS_ROOT].name[0],
           (unsigned)fs_entries[FS_ROOT].type,
           (unsigned)fs_entries[FS_ROOT].used);

    int etc = fs_mkdir(FS_ROOT, "etc");
    printf("[FS] mkdir /etc = %d\n", etc);
    if (etc >= 0) {
        int f = fs_create((uint8_t)etc, "motd");
        if (f >= 0) {
            const char *msg = "Welcome to MyOS!\nThis is MyFS.\n";
            fs_write(f, (const uint8_t*)msg, m_strlen(msg));
        }
        int f2 = fs_create((uint8_t)etc, "version");
        if (f2 >= 0) {
            const char *msg = "MyOS v0.4 with MyFS\n";
            fs_write(f2, (const uint8_t*)msg, m_strlen(msg));
        }
    }

    int home = fs_mkdir(FS_ROOT, "home");
    printf("[FS] mkdir /home = %d\n", home);
    if (home >= 0) {
        fs_mkdir((uint8_t)home, "user");
    }

    printf("[FS] format done, free_entries=%u\n", fs_free_entries());
}

int fs_load_from_disk(void) {
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
    printf("[FS] init start\n");

    int loaded = -1;
    if (sd_present()) {
        loaded = fs_load_from_disk();
        if (loaded == 0)
            printf("[FS] mounted existing filesystem\n");
        else
            printf("[FS] no valid FS on disk (err=%d), formatting...\n", loaded);
    }

    if (loaded != 0) {
        fs_format();
        if (sd_present()) {
            fs_sync_to_disk();
            printf("[FS] sync done\n");
        }
    }

    printf("[FS] init complete, free_entries=%u\n", fs_free_entries());
}

/* ============================================================
 * Поиск
 * ============================================================ */
int fs_find(uint8_t parent, const char *name) {
    if (!name || !*name) return -1;
    for (int i = 1; i < FS_MAX_ENTRIES; i++) {
        if (fs_entries[i].name[0] == 0) continue;
        if (fs_entries[i].parent != parent) continue;
        if (m_streq(fs_entries[i].name, name)) return i;
    }
    return -1;
}

static int fs_walk(const char *path, uint8_t start) {
    uint8_t cur = start;
    if (path[0] == '/') { cur = FS_ROOT; path++; }
    if (!*path) return cur;

    char name[FS_MAX_NAME];
    while (*path) {
        while (*path == '/') path++;
        if (!*path) break;
        int i = 0;
        while (*path && *path != '/') {
            if (i < FS_MAX_NAME - 1) name[i++] = *path;
            path++;
        }
        name[i] = 0;
        if (m_streq(name, ".")) continue;
        if (m_streq(name, "..")) {
            if (fs_entries[cur].parent != 0xFF) cur = fs_entries[cur].parent;
            continue;
        }
        int next = fs_find(cur, name);
        if (next < 0) return -1;
        cur = (uint8_t)next;
    }
    return (int)cur;
}

int fs_resolve(const char *path)        { return fs_walk(path, FS_ROOT); }
int fs_resolve_rel(const char *path, uint8_t cwd) { return fs_walk(path, cwd); }

/* ============================================================
 * Создание
 * ============================================================ */
static int fs_create_entry(uint8_t parent, const char *name, uint8_t type) {
    if (parent >= FS_MAX_ENTRIES) return -1;
    if (fs_entries[parent].name[0] == 0) return -1;
    if (fs_entries[parent].type != FT_DIR) return -1;
    if (!name || !*name) return -1;
    if (fs_find(parent, name) >= 0) return -1;

    int idx = fs_find_free_entry();
    if (idx < 0) return -1;

    fs_entry_t *e = &fs_entries[idx];
    /* Сначала обнуляем name — гарантируем что запись "свободна" пока не готова */
    e->name[0] = 0;
    e->used = 1;
    e->type = type;
    e->parent = parent;
    e->size = 0;
    e->first_block = FS_EOF;
    e->mtime = 0;
    /* name пишем ПОСЛЕДНЕЙ — это атомарная "публикация" записи */
    m_strcpy(e->name, name);
    return idx;
}

int fs_mkdir(uint8_t parent, const char *name)  { return fs_create_entry(parent, name, FT_DIR); }
int fs_create(uint8_t parent, const char *name) { return fs_create_entry(parent, name, FT_FILE); }

/* ============================================================
 * Запись / чтение
 * ============================================================ */
int fs_write(int idx, const uint8_t *buf, uint32_t len) {
    if (idx < 0 || idx >= FS_MAX_ENTRIES) return -1;
    fs_entry_t *e = &fs_entries[idx];
    if (!e->used || e->type != FT_FILE) return -1;

    if (e->first_block != FS_EOF) fs_free_chain(e->first_block);
    e->first_block = FS_EOF;
    e->size = 0;
    if (len == 0 || !buf) return 0;

    uint32_t written = 0;
    uint16_t prev = 0xFFFF, first = 0xFFFF;

    while (written < len) {
        int b = fs_alloc_block();
        if (b < 0) {
            if (first != 0xFFFF) fs_free_chain(first);
            e->first_block = FS_EOF;
            e->size = 0;
            return -1;
        }
        if (first == 0xFFFF) { first = (uint16_t)b; e->first_block = first; }
        else fs_fat[prev] = (uint16_t)b;
        prev = (uint16_t)b;

        uint32_t chunk = len - written;
        if (chunk > FS_BLOCK_SIZE) chunk = FS_BLOCK_SIZE;
        uint8_t *dst = &fs_data[(uint32_t)b * FS_BLOCK_SIZE];
        for (uint32_t i = 0; i < chunk; i++) dst[i] = buf[written + i];
        written += chunk;
    }
    e->size = len;
    return (int)len;
}

int fs_read(int idx, uint8_t *buf, uint32_t offset, uint32_t len) {
    if (idx < 0 || idx >= FS_MAX_ENTRIES) return -1;
    fs_entry_t *e = &fs_entries[idx];
    if (!e->used || e->type != FT_FILE) return -1;
    if (offset >= e->size) return 0;
    if (offset + len > e->size) len = e->size - offset;
    if (len == 0) return 0;

    uint32_t skip_blocks = offset / FS_BLOCK_SIZE;
    uint32_t skip_in_block = offset % FS_BLOCK_SIZE;

    uint16_t b = e->first_block;
    for (uint32_t i = 0; i < skip_blocks && b != FS_EOF; i++) {
        b = fs_fat[b];
    }
    if (b == FS_EOF) return 0;

    uint32_t got = 0;
    while (got < len && b != FS_EOF && b < FS_TOTAL_BLOCKS) {
        uint8_t *src = &fs_data[(uint32_t)b * FS_BLOCK_SIZE];
        uint32_t start = skip_in_block;
        uint32_t end = FS_BLOCK_SIZE;
        if (end - start > len - got) end = start + (len - got);
        for (uint32_t i = start; i < end; i++) buf[got++] = src[i];
        skip_in_block = 0;
        b = fs_fat[b];
    }
    return (int)got;
}

/* ============================================================
 * Удаление
 * ============================================================ */
int fs_delete(int idx) {
    if (idx <= FS_ROOT || idx >= FS_MAX_ENTRIES) return -1;
    fs_entry_t *e = &fs_entries[idx];
    if (e->name[0] == 0) return -1;

    if (e->type == FT_DIR) {
        for (int i = 1; i < FS_MAX_ENTRIES; i++) {
            if (fs_entries[i].used && fs_entries[i].parent == idx) return -1;
        }
    } else {
        if (e->first_block != FS_EOF) fs_free_chain(e->first_block);
    }
    e->used = 0;
    e->name[0] = 0;
    return 0;
}

/* ============================================================
 * Информация
 * ============================================================ */
uint32_t fs_total_blocks(void) { return FS_TOTAL_BLOCKS; }

uint32_t fs_free_blocks(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < FS_TOTAL_BLOCKS; i++)
        if (fs_fat[i] == FS_FREE_BLOCK) n++;
    return n;
}

uint32_t fs_total_entries(void) { return FS_MAX_ENTRIES; }

uint32_t fs_free_entries(void) {
    uint32_t n = 0;
    for (int i = 0; i < FS_MAX_ENTRIES; i++)
        if (fs_entries[i].name[0] == 0) n++;
    return n;
}

const char *fs_name(int idx) {
    if (idx < 0 || idx >= FS_MAX_ENTRIES) return "?";
    return fs_entries[idx].name;
}

uint8_t fs_type(int idx) {
    if (idx < 0 || idx >= FS_MAX_ENTRIES) return FT_FREE;
    if (fs_entries[idx].name[0] == 0) return FT_FREE;
    return fs_entries[idx].type;
}

uint32_t fs_size(int idx) {
    if (idx < 0 || idx >= FS_MAX_ENTRIES) return 0;
    return fs_entries[idx].size;
}

void fs_path(int idx, char *buf, int bufsize) {
    if (bufsize < 2) return;
    if (idx == FS_ROOT) { buf[0] = '/'; buf[1] = 0; return; }

    char temp[FS_MAX_PATH];
    int pos = FS_MAX_PATH - 1;
    temp[pos] = 0;
    int cur = idx;
    int guard = 0;
    while (cur != FS_ROOT && cur > 0 && pos > 0 && guard++ < FS_MAX_ENTRIES) {
        const char *n = fs_entries[cur].name;
        int nl = m_strlen(n);
        pos -= nl;
        if (pos < 0) pos = 0;
        for (int i = 0; i < nl && pos + i < FS_MAX_PATH - 1; i++)
            temp[pos + i] = n[i];
        if (pos > 0) temp[--pos] = '/';
        cur = fs_entries[cur].parent;
    }
    m_strcpy(buf, &temp[pos]);
}

int fs_list(uint8_t parent, int *out, int max) {
    int n = 0;
    for (int i = 1; i < FS_MAX_ENTRIES && n < max; i++) {
        if (fs_entries[i].name[0] == 0) continue;
        if (fs_entries[i].parent == parent) out[n++] = i;
    }
    return n;
}
