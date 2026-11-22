#ifndef MYFS_H
#define MYFS_H

#include <stdint.h>
#include <stddef.h>

#define MYFS_BLK      512
#define MYFS_INO_SIZE 64
#define MYFS_DIRECT   10
#define MYFS_MAGIC    "MYFS1"

#define MYFS_TYPE_FILE 1
#define MYFS_TYPE_DIR  2

typedef struct {
    uint32_t mode;
    uint32_t size;
    uint16_t nblocks;
    uint16_t direct[MYFS_DIRECT];
    uint16_t indirect;
    uint32_t mtime;
} myfs_inode_t;

int  myfs_mount(const char *devname);
void myfs_umount(void);

int  myfs_read(uint16_t ino, uint32_t off, void *buf, uint32_t len);
int  myfs_write(uint16_t ino, uint32_t off, const void *buf, uint32_t len);
int  myfs_create(const char *path, uint32_t mode, uint16_t *out_ino);
int  myfs_mkdir(const char *path, uint32_t mode, uint16_t *out_ino);
int  myfs_unlink(const char *path);
int  myfs_rename(const char *oldpath, const char *newpath);
int  myfs_readdir(uint16_t dir_ino, int idx, char *name, size_t namesz, uint16_t *ino, uint8_t *type);
int  myfs_stat(const char *path, myfs_inode_t *out);
int  myfs_resolve(const char *path, uint16_t *ino, uint8_t *type);
uint16_t myfs_root_ino(void);

/* Writeback */
void myfs_sync(void);

#endif
