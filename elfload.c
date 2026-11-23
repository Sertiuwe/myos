#include "elfload.h"
#include "syslib.h"
#include "syscall.h"
#include "sched.h"
#include "memory.h"
#include "kstring.h"
#include "kstdio.h"

#define USER_BASE 0x2000000ULL
#define USER_LIMIT 0x4000000ULL
#define PA(e) ((e) & 0x000FFFFFFFFFF000ULL)

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

typedef struct {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
} Elf64_Shdr;

typedef struct {
    char magic[8]; uint32_t version, flags, stack_size, heap_size; char name[32];
} app_meta_t;

typedef struct { char name[32]; uint32_t sysnum, flags; } app_import_t;

extern const unsigned char app_blob[];
extern const unsigned int app_blob_len;
extern void kernel_init_ring3(void);
extern int  kernel_user_spawn(uint64_t entry, uint64_t stack_top, const char *name);
extern int  kernel_user_wait(void);

static const struct { const char *name; uint32_t num; } kexports[] = {
    {"sl_open", SYS_OPEN}, {"sl_read", SYS_READ}, {"sl_write", SYS_WRITE},
    {"sl_close", SYS_CLOSE}, {"sl_seek", SYS_SEEK}, {"sl_tell", SYS_TELL},
    {"sl_mkdir", SYS_MKDIR}, {"sl_unlink", SYS_UNLINK}, {"sl_rename", SYS_RENAME},
    {"sl_readdir", SYS_READDIR}, {"sl_stat", SYS_STAT}, {"sl_sync", SYS_SYNC},
    {"sl_uptime_ms", SYS_UPTIME_MS}, {"sl_sleep_ms", SYS_SLEEP_MS},
    {"sl_yield", SYS_YIELD}, {"sl_tid", SYS_TID}, {"sl_task_name", SYS_TASKNAME},
    {"sl_datetime", SYS_DATETIME}, {"sl_sysstat", SYS_STATS},
    {"sl_kprint", SYS_KPRINT}, {"sl_exit", SYS_EXIT},
};

static int export_lookup(const char *name, uint32_t *out) {
    for (unsigned i = 0; i < sizeof(kexports) / sizeof(kexports[0]); i++) {
        if (!strcmp(name, kexports[i].name)) { *out = kexports[i].num; return 0; }
    }
    return -1;
}

/* map identity 2MB pages covering [va, va+len) as user-accessible */
static void user_map(uint64_t va, uint64_t len) {
    uint64_t cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)PA(cr3);
    uint64_t start = va & ~0x1FFFFFULL;
    uint64_t end = (va + len + 0x1FFFFFULL) & ~0x1FFFFFULL;
    for (uint64_t a = start; a < end; a += 0x200000) {
        uint64_t i4 = (a >> 39) & 0x1FF, i3 = (a >> 30) & 0x1FF, i2 = (a >> 21) & 0x1FF;
        if (!(pml4[i4] & 1)) {
            uint64_t *t = kmalloc(4096 + 4096);
            uint64_t pt = ((uint64_t)(uintptr_t)t + 4095) & ~4095ULL;
            for (int k = 0; k < 512; k++) ((uint64_t *)(uintptr_t)pt)[k] = 0;
            pml4[i4] = pt | 7;
        }
        pml4[i4] |= 4;
        uint64_t *pdpt = (uint64_t *)(uintptr_t)PA(pml4[i4]);
        if (!(pdpt[i3] & 1)) {
            uint64_t *t = kmalloc(4096 + 4096);
            uint64_t pt = ((uint64_t)(uintptr_t)t + 4095) & ~4095ULL;
            for (int k = 0; k < 512; k++) ((uint64_t *)(uintptr_t)pt)[k] = 0;
            pdpt[i3] = pt | 7;
        }
        pdpt[i3] |= 4;
        uint64_t *pd = (uint64_t *)(uintptr_t)PA(pdpt[i3]);
        if (!(pd[i2] & 1)) pd[i2] = a | 0x87;   /* present|rw|user|ps */
        pd[i2] |= 4;
    }
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static unsigned char *read_file(const char *path, uint32_t *out_len) {
    sl_stat_t st;
    if (sl_stat(path, &st) != SL_OK) return NULL;
    if (st.size == 0 || st.size > 1048576) return NULL;
    unsigned char *buf = kmalloc(st.size + 64);
    if (!buf) return NULL;
    int fd = sl_open(path, SL_O_READ);
    if (fd < 0) { kfree(buf); return NULL; }
    uint32_t got = 0;
    while (got < st.size) {
        int n = sl_read(fd, buf + got, st.size - got);
        if (n <= 0) break;
        got += (uint32_t)n;
    }
    sl_close(fd);
    *out_len = got;
    return buf;
}

static const Elf64_Shdr *find_section(const unsigned char *b, const Elf64_Ehdr *h, const char *name) {
    if (!h->e_shoff || !h->e_shnum) return NULL;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(b + h->e_shoff);
    const Elf64_Shdr *str = &sh[h->e_shstrndx];
    const char *names = (const char *)(b + str->sh_offset);
    for (int i = 0; i < h->e_shnum; i++)
        if (!strcmp(names + sh[i].sh_name, name)) return &sh[i];
    return NULL;
}

static int validate(const unsigned char *b, uint32_t len, const Elf64_Ehdr **outh) {
    if (len < sizeof(Elf64_Ehdr)) { kprintf("ELF: too small\n"); return -1; }
    const Elf64_Ehdr *h = (const Elf64_Ehdr *)b;
    if (b[0] != 0x7F || b[1] != 'E' || b[2] != 'L' || b[3] != 'F') { kprintf("ELF: bad magic\n"); return -1; }
    if (b[4] != 2 || b[5] != 1) { kprintf("ELF: need 64-bit LSB\n"); return -1; }
    if (h->e_machine != 62) { kprintf("ELF: not x86_64\n"); return -1; }
    if (h->e_type != 2) { kprintf("ELF: not ET_EXEC\n"); return -1; }
    *outh = h;
    return 0;
}

int elf_info(const char *path) {
    uint32_t len = 0;
    unsigned char *b = read_file(path, &len);
    if (!b) { kprintf("ELF: cannot read %s\n", path); return -1; }
    const Elf64_Ehdr *h;
    if (validate(b, len, &h)) { kfree(b); return -1; }
    kprintf("ELF %s: %u bytes, entry=%p, %u phdrs, %u shdrs\n",
            path, (unsigned)len, (void *)(uintptr_t)h->e_entry,
            (unsigned)h->e_phnum, (unsigned)h->e_shnum);
    const Elf64_Phdr *ph = (const Elf64_Phdr *)(b + h->e_phoff);
    for (int i = 0; i < h->e_phnum; i++) {
        if (ph[i].p_type != 1) continue;
        kprintf("  LOAD va=%p filesz=%u memsz=%u flags=%x\n",
                (void *)(uintptr_t)ph[i].p_vaddr, (unsigned)ph[i].p_filesz,
                (unsigned)ph[i].p_memsz, (unsigned)ph[i].p_flags);
    }
    const Elf64_Shdr *ms = find_section(b, h, ".myos.meta");
    if (ms) {
        const app_meta_t *m = (const app_meta_t *)(b + ms->sh_offset);
        kprintf("  meta: %.8s ver=%u stack=%u name=%s\n", m->magic,
                (unsigned)m->version, (unsigned)m->stack_size, m->name);
    } else kprintf("  meta: MISSING\n");
    const Elf64_Shdr *is = find_section(b, h, ".myos.imports");
    if (is) {
        unsigned n = (unsigned)(is->sh_size / sizeof(app_import_t));
        kprintf("  imports: %u\n", n);
        const app_import_t *im = (const app_import_t *)(b + is->sh_offset);
        for (unsigned i = 0; i < n; i++) {
            uint32_t num;
            int ok = export_lookup(im[i].name, &num);
            kprintf("    %s -> %s\n", im[i].name, ok ? "UNRESOLVED" : "ok");
        }
    } else kprintf("  imports: MISSING\n");
    kfree(b);
    return 0;
}

int elf_run(const char *path) {
    uint32_t len = 0;
    unsigned char *b = read_file(path, &len);
    if (!b) { kprintf("ELF: cannot read %s\n", path); return -1; }
    const Elf64_Ehdr *h;
    if (validate(b, len, &h)) { kfree(b); return -1; }

    const Elf64_Shdr *ms = find_section(b, h, ".myos.meta");
    if (!ms) { kprintf("ELF: .myos.meta missing - not a MyOS app\n"); kfree(b); return -1; }
    const app_meta_t *m = (const app_meta_t *)(b + ms->sh_offset);
    if (memcmp(m->magic, "MYOSAPP1", 8) || m->version != 1) {
        kprintf("ELF: bad app meta\n"); kfree(b); return -1;
    }

    /* resolve imports in the file image before copying */
    const Elf64_Shdr *is = find_section(b, h, ".myos.imports");
    unsigned nimp = 0;
    if (is) {
        nimp = (unsigned)(is->sh_size / sizeof(app_import_t));
        app_import_t *im = (app_import_t *)(b + is->sh_offset);
        for (unsigned i = 0; i < nimp; i++) {
            uint32_t num;
            if (export_lookup(im[i].name, &num)) {
                kprintf("ELF: unresolved import '%s'\n", im[i].name);
                kfree(b); return -1;
            }
            im[i].sysnum = num;
        }
    }

    const Elf64_Phdr *ph = (const Elf64_Phdr *)(b + h->e_phoff);
    for (int i = 0; i < h->e_phnum; i++) {
        if (ph[i].p_type != 1) continue;
        if (ph[i].p_vaddr < USER_BASE || ph[i].p_vaddr + ph[i].p_memsz > USER_LIMIT) {
            kprintf("ELF: segment outside user window\n"); kfree(b); return -1;
        }
        user_map(ph[i].p_vaddr, ph[i].p_memsz);
        memcpy((void *)(uintptr_t)ph[i].p_vaddr, b + ph[i].p_offset, ph[i].p_filesz);
        if (ph[i].p_memsz > ph[i].p_filesz)
            memset((void *)(uintptr_t)(ph[i].p_vaddr + ph[i].p_filesz), 0,
                   ph[i].p_memsz - ph[i].p_filesz);
    }

    uint32_t stack = m->stack_size ? m->stack_size : 16384;
    uint8_t *st = kmalloc(stack + 64);
    if (!st) { kprintf("ELF: stack alloc failed\n"); kfree(b); return -1; }
    uint64_t top = ((uint64_t)(uintptr_t)st + stack) & ~0xFULL;

    kprintf("ELF: loading '%s' entry=%p imports=%u\n", m->name,
            (void *)(uintptr_t)h->e_entry, nimp);
    char appname[32];
    strncpy(appname, m->name, sizeof(appname) - 1);
    appname[sizeof(appname) - 1] = '\0';
    uint64_t entry = h->e_entry;
    kfree(b);

    kernel_init_ring3();
    if (kernel_user_spawn(entry, top, appname) < 0) { kprintf("ELF: spawn failed\n"); return -1; }
    int rc = kernel_user_wait();
    kprintf("ELF: '%s' exited with code %d\n", appname, rc);
    return 0;
}

int elf_install_demo(void) {
    sl_stat_t st;
    if (sl_stat("/hello.elf", &st) == SL_OK) {
        kprintf("ELF: /bin/hello.elf already present, size=%u\n", (unsigned)st.size);
        if (st.size == app_blob_len) return 0;
        kprintf("ELF: size mismatch (want %u), reinstalling\n", (unsigned)app_blob_len);
        sl_unlink("/hello.elf");
    }
    int rc = sl_mkdir("/bin");
    kprintf("ELF: mkdir /bin -> %d\n", rc);
    int fd = sl_open("/hello.elf", SL_O_WRITE | SL_O_CREATE);
    kprintf("ELF: open -> %d\n", fd);
    if (fd < 0) return -1;
    unsigned done = 0;
    while (done < app_blob_len) {
        unsigned chunk = app_blob_len - done;
        if (chunk > 512) chunk = 512;
        int n = sl_write(fd, app_blob + done, chunk);
        if (n <= 0) {
            kprintf("ELF: write failed at %u (rc=%d)\n", done, n);
            sl_close(fd);
            return -1;
        }
        done += (unsigned)n;
    }
    sl_close(fd);
    sl_sync();
    sl_stat_t st2;
    int r2 = sl_stat("/hello.elf", &st2);
    kprintf("ELF: installed %u bytes, verify stat=%d size=%u\n",
            done, r2, r2 == SL_OK ? (unsigned)st2.size : 0u);
    return 0;
}
