#include "myfs.h"
#include "bdev.h"
#include "bcache.h"
#include "memory.h"
#include "kstring.h"
#include "rtc.h"
#include "kstdio.h"
#include <stdint.h>

/* On-disk superblock layout (packed) */
typedef struct __attribute__((packed)) {
    char     magic[8];
    uint32_t block_size;
    uint32_t total_blocks;
    uint16_t bm_start;
    uint16_t bm_len;
    uint16_t itab_start;
    uint16_t itab_len;
    uint32_t data_start;
    uint16_t inode_count;
    uint32_t root_ino;
} sb_disk_t;

static bdev_t   *fs_dev;
static sb_disk_t sb;
static uint8_t   mounted;

/* --- helpers --- */
static int read_blk(uint64_t lba, void *buf) { return bcache_read(fs_dev, lba, buf); }
static int write_blk(uint64_t lba, const void *buf) { return bcache_write(fs_dev, lba, buf); }

static uint32_t now_unix(void) {
    rtc_time_t t;
    if (rtc_read(&t) != 0) return 0;
    /* Простая конвертация без учёта високосных для начала */
    uint32_t days = (t.year - 1970) * 365 + (t.month - 1) * 30 + t.day - 1;
    return days * 86400 + t.hour * 3600 + t.min * 60 + t.sec;
}

/* --- inode I/O --- */
static int read_inode(uint16_t ino, myfs_inode_t *out) {
    if (!mounted || ino >= sb.inode_count) return -1;
    uint32_t blk_idx = ino / (MYFS_BLK / MYFS_INO_SIZE);
    uint32_t off = (ino % (MYFS_BLK / MYFS_INO_SIZE)) * MYFS_INO_SIZE;
    uint8_t buf[MYFS_BLK];
    if (read_blk(sb.itab_start + blk_idx, buf)) return -1;
    memcpy(out, buf + off, sizeof(*out));
    return 0;
}

static int write_inode(uint16_t ino, const myfs_inode_t *in) {
    if (!mounted || ino >= sb.inode_count) return -1;
    uint32_t blk_idx = ino / (MYFS_BLK / MYFS_INO_SIZE);
    uint32_t off = (ino % (MYFS_BLK / MYFS_INO_SIZE)) * MYFS_INO_SIZE;
    uint8_t buf[MYFS_BLK];
    if (read_blk(sb.itab_start + blk_idx, buf)) return -1;
    memcpy(buf + off, in, sizeof(*in));
    return write_blk(sb.itab_start + blk_idx, buf);
}

/* --- bitmap --- */
static int bm_get(uint32_t bit) {
    uint32_t byte_off = bit / 8;
    uint32_t blk_idx = byte_off / MYFS_BLK;
    uint32_t in_blk = byte_off % MYFS_BLK;
    uint8_t buf[MYFS_BLK];
    if (read_blk(sb.bm_start + blk_idx, buf)) return -1;
    return (buf[in_blk] >> (bit % 8)) & 1;
}

static int bm_set(uint32_t bit, int val) {
    uint32_t byte_off = bit / 8;
    uint32_t blk_idx = byte_off / MYFS_BLK;
    uint32_t in_blk = byte_off % MYFS_BLK;
    uint8_t buf[MYFS_BLK];
    if (read_blk(sb.bm_start + blk_idx, buf)) return -1;
    if (val) buf[in_blk] |= (1 << (bit % 8));
    else     buf[in_blk] &= ~(1 << (bit % 8));
    return write_blk(sb.bm_start + blk_idx, buf);
}

static int alloc_block(void) {
    for (uint32_t i = sb.data_start; i < sb.total_blocks; i++) {
        int v = bm_get(i);
        if (v == 0) { bm_set(i, 1); return (int)i; }
    }
    return -1;
}

static void free_block(uint32_t blk) { bm_set(blk, 0); }

static int alloc_inode(void) {
    myfs_inode_t ino;
    for (uint16_t i = 3; i < sb.inode_count; i++) {  /* 0=free, 1=rsv, 2=root */
        if (read_inode(i, &ino)) continue;
        if (ino.mode == 0) return (int)i;
    }
    return -1;
}

/* --- file data I/O --- */
static uint16_t inode_block(const myfs_inode_t *in, uint32_t blk_idx) {
    if (blk_idx < MYFS_DIRECT) return in->direct[blk_idx];
    if (in->indirect == 0) return 0;
    uint32_t ind_idx = blk_idx - MYFS_DIRECT;
    if (ind_idx >= MYFS_BLK / 2) return 0;
    uint16_t table[MYFS_BLK / 2];
    if (read_blk(in->indirect, table)) return 0;
    return table[ind_idx];
}

int myfs_read(uint16_t ino_num, uint32_t off, void *buf, uint32_t len) {
    myfs_inode_t ino;
    if (read_inode(ino_num, &ino)) return -1;
    if (off >= ino.size) return 0;
    if (off + len > ino.size) len = ino.size - off;
    uint8_t *p = (uint8_t *)buf;
    uint32_t done = 0;
    while (done < len) {
        uint32_t blk_idx = (off + done) / MYFS_BLK;
        uint32_t blk_off = (off + done) % MYFS_BLK;
        uint16_t db = inode_block(&ino, blk_idx);
        if (!db) break;
        uint8_t tmp[MYFS_BLK];
        if (read_blk(db, tmp)) return -1;
        uint32_t chunk = MYFS_BLK - blk_off;
        if (chunk > len - done) chunk = len - done;
        memcpy(p + done, tmp + blk_off, chunk);
        done += chunk;
    }
    return (int)done;
}

int myfs_write(uint16_t ino_num, uint32_t off, const void *buf, uint32_t len) {
    myfs_inode_t ino;
    if (read_inode(ino_num, &ino)) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t done = 0;
    while (done < len) {
        uint32_t blk_idx = (off + done) / MYFS_BLK;
        uint32_t blk_off = (off + done) % MYFS_BLK;
        uint16_t db = inode_block(&ino, blk_idx);
        if (!db) {
            int nb = alloc_block();
            if (nb < 0) return -1;
            if (blk_idx < MYFS_DIRECT) {
                ino.direct[blk_idx] = (uint16_t)nb;
            } else {
                if (!ino.indirect) {
                    int ib = alloc_block();
                    if (ib < 0) { free_block((uint32_t)nb); return -1; }
                    uint8_t z[MYFS_BLK]; memset(z, 0, MYFS_BLK);
                    write_blk((uint64_t)ib, z);
                    ino.indirect = (uint16_t)ib;
                }
                uint16_t table[MYFS_BLK / 2];
                read_blk(ino.indirect, table);
                table[blk_idx - MYFS_DIRECT] = (uint16_t)nb;
                write_blk(ino.indirect, table);
            }
            ino.nblocks++;
            db = (uint16_t)nb;
            /* zero new block if writing partial */
            if (blk_off != 0 || done + MYFS_BLK < len) {
                uint8_t z[MYFS_BLK]; memset(z, 0, MYFS_BLK);
                write_blk(db, z);
            }
        }
        uint8_t tmp[MYFS_BLK];
        uint32_t chunk = MYFS_BLK - blk_off;
        if (chunk > len - done) chunk = len - done;
        if (blk_off != 0 || chunk < MYFS_BLK) {
            if (read_blk(db, tmp)) return -1;
        }
        memcpy(tmp + blk_off, p + done, chunk);
        if (write_blk(db, tmp)) return -1;
        done += chunk;
    }
    if (off + len > ino.size) ino.size = off + len;
    ino.mtime = now_unix();
    write_inode(ino_num, &ino);
    return (int)done;
}

/* --- directory ops --- */
/* DirEntry on-disk: ino(2) + type(1) + namelen(1) + name(namelen) */
typedef struct __attribute__((packed)) {
    uint16_t ino;
    uint8_t  type;   /* 1=file, 2=dir */
    uint8_t  namelen;
} dirent_hdr_t;

static int dir_lookup(uint16_t dir_ino, const char *name, uint16_t *out_ino, uint8_t *out_type) {
    myfs_inode_t din;
    if (read_inode(dir_ino, &din)) return -1;
    uint32_t pos = 0;
    while (pos < din.size) {
        dirent_hdr_t hdr;
        if (myfs_read(dir_ino, pos, &hdr, sizeof(hdr)) != sizeof(hdr)) break;
        if (hdr.ino == 0)     { pos += (uint32_t)sizeof(hdr) + hdr.namelen; continue; }
        if (hdr.namelen == 0) { pos += (uint32_t)sizeof(hdr); continue; }
        char ename[256];
        if (myfs_read(dir_ino, pos + sizeof(hdr), ename, hdr.namelen) != (int)hdr.namelen) break;
        ename[hdr.namelen] = '\0';
        if (strcmp(ename, name) == 0) {
            if (out_ino) *out_ino = hdr.ino;
            if (out_type) *out_type = hdr.type;
            return 0;
        }
        pos += sizeof(hdr) + hdr.namelen;
    }
    return -1;
}

static int dir_add_entry(uint16_t dir_ino, const char *name, uint16_t ino, uint8_t type) {
    dirent_hdr_t hdr;
    hdr.ino = ino;
    hdr.type = type;
    hdr.namelen = (uint8_t)strlen(name);
    myfs_inode_t din;
    if (read_inode(dir_ino, &din)) return -1;

    /* reuse a free slot (ino==0) that fits */
    uint32_t pos = 0, slot = 0xFFFFFFFFu;
    while (pos < din.size) {
        dirent_hdr_t h;
        if (myfs_read(dir_ino, pos, &h, sizeof(h)) != (int)sizeof(h)) break;
        if (h.ino == 0 && h.namelen >= hdr.namelen) { slot = pos; break; }
        if (h.namelen == 0) { pos += (uint32_t)sizeof(h); continue; }
        pos += (uint32_t)sizeof(h) + h.namelen;
    }
    if (slot != 0xFFFFFFFFu) {
        if (myfs_write(dir_ino, slot, &hdr, sizeof(hdr)) < 0) return -1;
        if (myfs_write(dir_ino, slot + sizeof(hdr), name, hdr.namelen) < 0) return -1;
        return 0;
    }

    pos = din.size;
    if (myfs_write(dir_ino, pos, &hdr, sizeof(hdr)) < 0) return -1;
    if (myfs_write(dir_ino, pos + sizeof(hdr), name, hdr.namelen) < 0) return -1;
    din.size = pos + (uint32_t)sizeof(hdr) + hdr.namelen;   /* the missing update */
    return write_inode(dir_ino, &din);
}

static int resolve_path(const char *path, uint16_t *out_ino, uint8_t *out_type) {
    uint16_t cur = sb.root_ino;
    uint8_t tp = 2;
    if (path[0] == '/') path++;
    if (*path == '\0') { *out_ino = cur; if (out_type) *out_type = tp; return 0; }
    char comp[256];
    while (*path) {
        const char *sl = strchr(path, '/');
        size_t len = sl ? (size_t)(sl - path) : strlen(path);
        if (len >= sizeof(comp)) return -1;
        memcpy(comp, path, len); comp[len] = '\0';
        uint16_t next; uint8_t nt;
        if (dir_lookup(cur, comp, &next, &nt)) return -1;
        cur = next; tp = nt;
        path += len;
        if (*path == '/') path++;
    }
    *out_ino = cur;
    if (out_type) *out_type = tp;
    return 0;
}

int myfs_stat(const char *path, myfs_inode_t *out) {
    uint16_t ino;
    if (resolve_path(path, &ino, NULL)) return -1;
    return read_inode(ino, out);
}

int myfs_create(const char *path, uint32_t mode, uint16_t *out_ino) {
    /* Разбираем путь на parent + name */
    const char *last_slash = strrchr(path, '/');
    char parent[256], name[256];
    if (last_slash) {
        size_t plen = (size_t)(last_slash - path);
        if (plen == 0) plen = 1;  /* "/" */
        memcpy(parent, path, plen); parent[plen] = '\0';
        strcpy(name, last_slash + 1);
    } else {
        strcpy(parent, "/");
        strcpy(name, path);
    }
    if (name[0] == '\0') return -1;

    uint16_t dir_ino; uint8_t dt;
    if (resolve_path(parent, &dir_ino, &dt)) return -1;
    if (dt != 2) return -1;  /* parent не директория */

    /* Проверяем, не существует ли уже */
    uint16_t dummy;
    if (dir_lookup(dir_ino, name, &dummy, NULL) == 0) return -1;

    int ni = alloc_inode();
    if (ni < 0) return -1;
    myfs_inode_t new_ino;
    memset(&new_ino, 0, sizeof(new_ino));
    new_ino.mode = mode;
    new_ino.mtime = now_unix();
    write_inode((uint16_t)ni, &new_ino);

    if (dir_add_entry(dir_ino, name, (uint16_t)ni, 1)) return -1;
    if (out_ino) *out_ino = (uint16_t)ni;
    return 0;
}

int myfs_mkdir(const char *path, uint32_t mode, uint16_t *out_ino) {
    const char *last_slash = strrchr(path, '/');
    char parent[256], name[256];
    if (last_slash) {
        size_t plen = (size_t)(last_slash - path);
        if (plen == 0) plen = 1;
        memcpy(parent, path, plen); parent[plen] = '\0';
        strcpy(name, last_slash + 1);
    } else {
        strcpy(parent, "/"); strcpy(name, path);
    }
    if (name[0] == '\0') return -1;

    uint16_t dir_ino; uint8_t dt;
    if (resolve_path(parent, &dir_ino, &dt)) return -1;
    if (dt != MYFS_TYPE_DIR) return -1;

    uint16_t dummy;
    if (dir_lookup(dir_ino, name, &dummy, NULL) == 0) return -1;

    int ni = alloc_inode();
    if (ni < 0) return -1;

    int db = alloc_block();
    if (db < 0) return -1;
    uint8_t z[MYFS_BLK]; memset(z, 0, MYFS_BLK);
    write_blk((uint64_t)db, z);

    myfs_inode_t new_ino;
    memset(&new_ino, 0, sizeof(new_ino));
    new_ino.mode = mode | 0040000;
    new_ino.size = 0;
    new_ino.nblocks = 1;
    new_ino.direct[0] = (uint16_t)db;
    new_ino.mtime = now_unix();
    write_inode((uint16_t)ni, &new_ino);

    if (dir_add_entry(dir_ino, name, (uint16_t)ni, MYFS_TYPE_DIR)) return -1;
    if (out_ino) *out_ino = (uint16_t)ni;
    return 0;
}

int myfs_unlink(const char *path) {
    uint16_t ino; uint8_t tp;
    if (resolve_path(path, &ino, &tp)) return -1;
    if (tp == 2) return -1;  /* не удаляем директории пока */

    /* Очищаем иноду */
    myfs_inode_t node;
    read_inode(ino, &node);
    for (int i = 0; i < MYFS_DIRECT && node.direct[i]; i++)
        free_block(node.direct[i]);
    if (node.indirect) {
        uint16_t table[MYFS_BLK / 2];
        read_blk(node.indirect, table);
        for (int i = 0; i < MYFS_BLK / 2 && table[i]; i++)
            free_block(table[i]);
        free_block(node.indirect);
    }
    memset(&node, 0, sizeof(node));
    write_inode(ino, &node);

    /* Удаляем запись из родительского каталога (упрощённо: помечаем namelen=0) */
    const char *last_slash = strrchr(path, '/');
    char parent[256], name[256];
    if (last_slash) {
        size_t plen = (size_t)(last_slash - path);
        if (plen == 0) plen = 1;
        memcpy(parent, path, plen); parent[plen] = '\0';
        strcpy(name, last_slash + 1);
    } else {
        strcpy(parent, "/"); strcpy(name, path);
    }
    uint16_t dir_ino;
    resolve_path(parent, &dir_ino, NULL);
    myfs_inode_t din;
    read_inode(dir_ino, &din);
    uint32_t pos = 0;
    while (pos < din.size) {
        dirent_hdr_t hdr;
        if (myfs_read(dir_ino, pos, &hdr, sizeof(hdr)) != sizeof(hdr)) break;
        if (hdr.ino == 0)     { pos += (uint32_t)sizeof(hdr) + hdr.namelen; continue; }
        if (hdr.namelen == 0) { pos += (uint32_t)sizeof(hdr); continue; }
        char ename[256];
        myfs_read(dir_ino, pos + sizeof(hdr), ename, hdr.namelen);
        ename[hdr.namelen] = '\0';
        if (strcmp(ename, name) == 0 && hdr.ino == ino) {
            hdr.ino = 0; hdr.ino = 0;
            myfs_write(dir_ino, pos, &hdr, sizeof(hdr));
            break;
        }
        pos += sizeof(hdr) + hdr.namelen;
    }
    return 0;
}

int myfs_readdir(uint16_t dir_ino, int idx, char *name, size_t namesz, uint16_t *ino, uint8_t *type) {
    myfs_inode_t din;
    if (read_inode(dir_ino, &din)) return -1;
    uint32_t pos = 0;
    int cur_idx = 0;
    while (pos < din.size) {
        dirent_hdr_t hdr;
        if (myfs_read(dir_ino, pos, &hdr, sizeof(hdr)) != sizeof(hdr)) break;
        if (hdr.ino == 0)     { pos += (uint32_t)sizeof(hdr) + hdr.namelen; continue; }
        if (hdr.namelen == 0) { pos += (uint32_t)sizeof(hdr); continue; }
        if (cur_idx == idx) {
            size_t copy = hdr.namelen < namesz - 1 ? hdr.namelen : namesz - 1;
            myfs_read(dir_ino, pos + sizeof(hdr), name, (uint32_t)copy);
            name[copy] = '\0';
            if (ino) *ino = hdr.ino;
            if (type) *type = hdr.type;
            return 0;
        }
        cur_idx++;
        pos += sizeof(hdr) + hdr.namelen;
    }
    return -1;  /* no more entries */
}

/* --- mount --- */
int myfs_mount(const char *devname) {
    fs_dev = bdev_by_name(devname);
    if (!fs_dev) return -1;
    uint8_t buf[MYFS_BLK];
    if (bcache_read(fs_dev, 0, buf)) return -1;
    memcpy(&sb, buf, sizeof(sb));
    if (memcmp(sb.magic, MYFS_MAGIC, 5)) { kprintf("myfs: bad magic\n"); return -1; }
    mounted = 1;
    kprintf("myfs: mounted on %s (%u blocks, %u inodes, data@%u)\n",
            devname, sb.total_blocks, sb.inode_count, sb.data_start);
    return 0;
}

void myfs_umount(void) { mounted = 0; fs_dev = NULL; }
int myfs_rename(const char *oldpath, const char *newpath) {
    uint16_t old_ino; uint8_t old_type;
    if (resolve_path(oldpath, &old_ino, &old_type)) return -1;

    const char *last_slash = strrchr(newpath, '/');
    char parent[256], name[256];
    if (last_slash) {
        size_t plen = (size_t)(last_slash - newpath);
        if (plen == 0) plen = 1;
        memcpy(parent, newpath, plen); parent[plen] = '\0';
        strcpy(name, last_slash + 1);
    } else {
        strcpy(parent, "/"); strcpy(name, newpath);
    }
    if (name[0] == '\0') return -1;

    uint16_t new_dir_ino; uint8_t ndt;
    if (resolve_path(parent, &new_dir_ino, &ndt)) return -1;
    if (ndt != MYFS_TYPE_DIR) return -1;

    /* Если цель существует и это файл — удаляем */
    uint16_t exist_ino; uint8_t et;
    if (dir_lookup(new_dir_ino, name, &exist_ino, &et) == 0) {
        if (et == MYFS_TYPE_DIR) return -1;
        myfs_inode_t en; read_inode(exist_ino, &en);
        for (int i = 0; i < MYFS_DIRECT && en.direct[i]; i++) free_block(en.direct[i]);
        if (en.indirect) {
            uint16_t tbl[MYFS_BLK/2]; read_blk(en.indirect, tbl);
            for (int i = 0; i < MYFS_BLK/2 && tbl[i]; i++) free_block(tbl[i]);
            free_block(en.indirect);
        }
        memset(&en, 0, sizeof(en)); write_inode(exist_ino, &en);
        myfs_inode_t din; read_inode(new_dir_ino, &din);
        uint32_t pos = 0;
        while (pos < din.size) {
            dirent_hdr_t hdr;
            if (myfs_read(new_dir_ino, pos, &hdr, sizeof(hdr)) != (int)sizeof(hdr)) break;
            if (hdr.ino == 0)     { pos += (uint32_t)sizeof(hdr) + hdr.namelen; continue; }
        if (hdr.namelen == 0) { pos += (uint32_t)sizeof(hdr); continue; }
            char ename[256]; myfs_read(new_dir_ino, pos+sizeof(hdr), ename, hdr.namelen);
            ename[hdr.namelen] = '\0';
            if (strcmp(ename, name) == 0 && hdr.ino == exist_ino) {
                hdr.ino = 0; hdr.ino = 0;
                myfs_write(new_dir_ino, pos, &hdr, sizeof(hdr)); break;
            }
            pos += sizeof(hdr) + hdr.namelen;
        }
    }

    /* Удаляем из старого родителя */
    const char *old_slash = strrchr(oldpath, '/');
    char old_parent[256], old_name[256];
    if (old_slash) {
        size_t plen = (size_t)(old_slash - oldpath);
        if (plen == 0) plen = 1;
        memcpy(old_parent, oldpath, plen); old_parent[plen] = '\0';
        strcpy(old_name, old_slash + 1);
    } else {
        strcpy(old_parent, "/"); strcpy(old_name, oldpath);
    }
    uint16_t old_dir_ino; resolve_path(old_parent, &old_dir_ino, NULL);
    myfs_inode_t odin; read_inode(old_dir_ino, &odin);
    uint32_t pos = 0;
    while (pos < odin.size) {
        dirent_hdr_t hdr;
        if (myfs_read(old_dir_ino, pos, &hdr, sizeof(hdr)) != (int)sizeof(hdr)) break;
        if (hdr.ino == 0)     { pos += (uint32_t)sizeof(hdr) + hdr.namelen; continue; }
        if (hdr.namelen == 0) { pos += (uint32_t)sizeof(hdr); continue; }
        char ename[256]; myfs_read(old_dir_ino, pos+sizeof(hdr), ename, hdr.namelen);
        ename[hdr.namelen] = '\0';
        if (strcmp(ename, old_name) == 0 && hdr.ino == old_ino) {
            hdr.ino = 0; hdr.ino = 0;
            myfs_write(old_dir_ino, pos, &hdr, sizeof(hdr)); break;
        }
        pos += sizeof(hdr) + hdr.namelen;
    }

    dir_add_entry(new_dir_ino, name, old_ino, old_type);
    return 0;
}

void myfs_sync(void) {
    if (!mounted || !fs_dev) return;
    bcache_flush(fs_dev);
}

int myfs_read_inode_raw(uint16_t ino, myfs_inode_t *out) {
    return read_inode(ino, out);
}

uint16_t myfs_root_ino(void) { return sb.root_ino; }

int myfs_resolve(const char *path, uint16_t *ino, uint8_t *type) {
    return resolve_path(path, ino, type);
}
