#include "syscall.h"
#include "myfs.h"
#include "fslock.h"
#include "sched.h"
#include "pit.h"
#include "rtc.h"
#include "kstring.h"

extern int myfs_read_inode_raw(uint16_t ino, myfs_inode_t *out);

#define FD_MAX 64

typedef struct {
    int      used;
    uint16_t ino;
    uint8_t  type;
    uint32_t pos;
    uint32_t flags;
} fd_t;

static fd_t     fds[FD_MAX];
static uint32_t sys_calls[SYS_MAX + 1];

static int fd_alloc(void) {
    for (int i = 0; i < FD_MAX; i++) {
        if (fds[i].used) continue;
        memset(&fds[i], 0, sizeof(fd_t));
        fds[i].used = 1;
        return i;
    }
    return SL_ENOMEM;
}

static fd_t *fd_get(int fd) {
    if (fd < 0 || fd >= FD_MAX || !fds[fd].used) return NULL;
    return &fds[fd];
}

int64_t sys_dispatch(uint32_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    if (num >= 1 && num <= SYS_MAX) sys_calls[num]++;

    switch (num) {
    case SYS_OPEN: {
        const char *path = (const char *)(uintptr_t)a1;
        uint32_t flags = (uint32_t)a2;
        if (!path || !path[0]) return SL_EINVAL;
        uint16_t ino = 0;
        uint8_t ty = 0;
        fs_lock();
        int rc = myfs_resolve(path, &ino, &ty);
        if (rc != 0 && (flags & SL_O_CREATE)) {
            rc = myfs_create(path, 0644, &ino);
            ty = MYFS_TYPE_FILE;
        }
        fs_unlock();
        if (rc != 0) return SL_ENOENT;
        int fd = fd_alloc();
        if (fd < 0) return fd;
        fds[fd].ino = ino;
        fds[fd].type = ty;
        fds[fd].pos = 0;
        fds[fd].flags = flags;
        return fd;
    }
    case SYS_READ: {
        fd_t *f = fd_get((int)a1);
        void *buf = (void *)(uintptr_t)a2;
        uint32_t len = (uint32_t)a3;
        if (!f || !buf) return f ? SL_EINVAL : SL_EBADF;
        if (f->type != MYFS_TYPE_FILE) return SL_ENOTDIR;
        fs_lock();
        int got = myfs_read(f->ino, f->pos, buf, len);
        fs_unlock();
        if (got < 0) return SL_EGENERIC;
        f->pos += (uint32_t)got;
        return got;
    }
    case SYS_WRITE: {
        fd_t *f = fd_get((int)a1);
        const void *buf = (const void *)(uintptr_t)a2;
        uint32_t len = (uint32_t)a3;
        if (!f || !buf) return f ? SL_EINVAL : SL_EBADF;
        if (f->type != MYFS_TYPE_FILE) return SL_ENOTDIR;
        fs_lock();
        int put = myfs_write(f->ino, f->pos, buf, len);
        fs_unlock();
        if (put < 0) return SL_ENOSPC;
        f->pos += (uint32_t)put;
        return put;
    }
    case SYS_CLOSE: {
        fd_t *f = fd_get((int)a1);
        if (!f) return SL_EBADF;
        f->used = 0;
        return SL_OK;
    }
    case SYS_SEEK: {
        fd_t *f = fd_get((int)a1);
        if (!f) return SL_EBADF;
        f->pos = (uint32_t)a2;
        return SL_OK;
    }
    case SYS_TELL: {
        fd_t *f = fd_get((int)a1);
        if (!f) return SL_EBADF;
        return f->pos;
    }
    case SYS_MKDIR: {
        const char *path = (const char *)(uintptr_t)a1;
        if (!path || !path[0]) return SL_EINVAL;
        fs_lock();
        int rc = myfs_mkdir(path, 0755, NULL);
        fs_unlock();
        return rc == 0 ? SL_OK : SL_EEXIST;
    }
    case SYS_UNLINK: {
        const char *path = (const char *)(uintptr_t)a1;
        if (!path || !path[0]) return SL_EINVAL;
        fs_lock();
        int rc = myfs_unlink(path);
        fs_unlock();
        return rc == 0 ? SL_OK : SL_ENOENT;
    }
    case SYS_RENAME: {
        const char *o = (const char *)(uintptr_t)a1;
        const char *n = (const char *)(uintptr_t)a2;
        if (!o || !n) return SL_EINVAL;
        fs_lock();
        int rc = myfs_rename(o, n);
        fs_unlock();
        return rc == 0 ? SL_OK : SL_ENOENT;
    }
    case SYS_READDIR: {
        fd_t *f = fd_get((int)a1);
        sl_dirent_t *out = (sl_dirent_t *)(uintptr_t)a3;
        if (!f) return SL_EBADF;
        if (!out) return SL_EINVAL;
        if (f->type != MYFS_TYPE_DIR) return SL_ENOTDIR;
        char nm[SL_NAME_MAX];
        uint16_t ino = 0;
        uint8_t ty = 0;
        fs_lock();
        int rc = myfs_readdir(f->ino, (int)a2, nm, sizeof(nm), &ino, &ty);
        if (rc == 0) {
            memset(out, 0, sizeof(*out));
            strncpy(out->name, nm, SL_NAME_MAX - 1);
            out->type = ty;
            myfs_inode_t st;
            if (myfs_read_inode_raw(ino, &st) == 0) {
                out->size = st.size;
                out->mtime = st.mtime;
            }
        }
        fs_unlock();
        return rc == 0 ? SL_OK : SL_ENOENT;   /* ENOENT here = end of directory */
    }
    case SYS_STAT: {
        const char *path = (const char *)(uintptr_t)a1;
        sl_stat_t *out = (sl_stat_t *)(uintptr_t)a2;
        if (!path || !out) return SL_EINVAL;
        uint16_t ino = 0;
        uint8_t ty = 0;
        fs_lock();
        int rc = myfs_resolve(path, &ino, &ty);
        if (rc == 0) {
            myfs_inode_t st;
            rc = myfs_read_inode_raw(ino, &st);
            if (rc == 0) {
                out->size = st.size;
                out->mtime = st.mtime;
                out->type = ty;
            }
        }
        fs_unlock();
        return rc == 0 ? SL_OK : SL_ENOENT;
    }
    case SYS_SYNC:
        fs_lock();
        myfs_sync();
        fs_unlock();
        return SL_OK;
    case SYS_UPTIME_MS: {
        uint32_t hz = pit_hz();
        return hz ? (int64_t)(pit_ticks() * 1000 / hz) : 0;
    }
    case SYS_SLEEP_MS:
        sched_sleep_ms((uint32_t)a1);
        return SL_OK;
    case SYS_YIELD:
        sched_yield();
        return SL_OK;
    case SYS_TID:
        return sched_current_id();
    case SYS_TASKNAME: {
        char *buf = (char *)(uintptr_t)a1;
        uint32_t sz = (uint32_t)a2;
        if (!buf || !sz) return SL_EINVAL;
        const char *n = sched_current_name();
        strncpy(buf, n, sz - 1);
        buf[sz - 1] = '\0';
        return (int64_t)strlen(buf);
    }
    case SYS_DATETIME: {
        sl_datetime_t *out = (sl_datetime_t *)(uintptr_t)a1;
        if (!out) return SL_EINVAL;
        rtc_time_t rt;
        rtc_read(&rt);
                out->year = rt.year; out->month = rt.month; out->day = rt.day;
        out->hour = rt.hour; out->min = rt.min; out->sec = rt.sec;
        return SL_OK;
    }
    case SYS_STATS: {
        int idx = (int)a1;
        sl_sysstat_t *out = (sl_sysstat_t *)(uintptr_t)a2;
        if (idx < 0 || idx > SYS_MAX || !out) return SL_EINVAL;
        out->num = (uint32_t)idx;
        out->calls = sys_calls[idx];
        out->name = sys_name((uint32_t)idx);
        return SL_OK;
    }
    default:
        return SL_EINVAL;
    }
}

const char *sys_name(uint32_t num) {
    static const char *names[SYS_MAX + 1] = {
        [SYS_OPEN] = "open",       [SYS_READ] = "read",
        [SYS_WRITE] = "write",     [SYS_CLOSE] = "close",
        [SYS_SEEK] = "seek",       [SYS_TELL] = "tell",
        [SYS_MKDIR] = "mkdir",     [SYS_UNLINK] = "unlink",
        [SYS_RENAME] = "rename",   [SYS_READDIR] = "readdir",
        [SYS_STAT] = "stat",       [SYS_SYNC] = "sync",
        [SYS_UPTIME_MS] = "uptime_ms", [SYS_SLEEP_MS] = "sleep_ms",
        [SYS_YIELD] = "yield",     [SYS_TID] = "tid",
        [SYS_TASKNAME] = "taskname", [SYS_DATETIME] = "datetime",
        [SYS_STATS] = "stats",
    };
    if (num > SYS_MAX || !names[num]) return "?";
    return names[num];
}
