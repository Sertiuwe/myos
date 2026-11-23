#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* The syscall ABI. Today syslib calls sys_dispatch() directly (ring 0).
   Tomorrow each wrapper becomes `int 0x80` and sys_dispatch becomes the
   handler: no application code changes. */
#define SYS_OPEN       1
#define SYS_READ       2
#define SYS_WRITE      3
#define SYS_CLOSE      4
#define SYS_SEEK       5
#define SYS_TELL       6
#define SYS_MKDIR      7
#define SYS_UNLINK     8
#define SYS_RENAME     9
#define SYS_READDIR   10
#define SYS_STAT      11
#define SYS_SYNC      12
#define SYS_UPTIME_MS 13
#define SYS_SLEEP_MS  14
#define SYS_YIELD     15
#define SYS_TID       16
#define SYS_TASKNAME  17
#define SYS_DATETIME  18
#define SYS_STATS     19
#define SYS_KPRINT    20
#define SYS_EXIT      21
#define SYS_MAX       21

#define SL_O_READ   0x1
#define SL_O_WRITE  0x2
#define SL_O_CREATE 0x4

#define SL_OK        0
#define SL_EGENERIC (-1)
#define SL_ENOENT   (-2)
#define SL_EEXIST   (-3)
#define SL_ENOTDIR  (-4)
#define SL_EBADF    (-5)
#define SL_EINVAL   (-6)
#define SL_ENOSPC   (-7)
#define SL_ENOMEM   (-8)

#define SL_NAME_MAX 64
#define SL_TYPE_FILE 1
#define SL_TYPE_DIR  2

typedef struct {
    uint32_t size;
    uint32_t mtime;
    uint8_t  type;
} sl_stat_t;

typedef struct {
    char     name[SL_NAME_MAX];
    uint32_t size;
    uint32_t mtime;
    uint8_t  type;
} sl_dirent_t;

typedef struct {
    uint16_t year;
    uint8_t  month, day, hour, min, sec;
} sl_datetime_t;

typedef struct {
    uint32_t    num;
    uint32_t    calls;
    const char *name;
} sl_sysstat_t;

int64_t sys_dispatch(uint32_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5, uint64_t a6);
const char *sys_name(uint32_t num);

#endif
