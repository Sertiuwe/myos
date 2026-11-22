#include "syslib.h"

static uint64_t P(const void *p) { return (uint64_t)(uintptr_t)p; }

int sl_open(const char *path, uint32_t flags) {
    return (int)sys_dispatch(SYS_OPEN, P(path), flags, 0, 0, 0, 0);
}
int sl_read(int fd, void *buf, uint32_t len) {
    return (int)sys_dispatch(SYS_READ, (uint64_t)fd, P(buf), len, 0, 0, 0);
}
int sl_write(int fd, const void *buf, uint32_t len) {
    return (int)sys_dispatch(SYS_WRITE, (uint64_t)fd, P(buf), len, 0, 0, 0);
}
int sl_close(int fd) {
    return (int)sys_dispatch(SYS_CLOSE, (uint64_t)fd, 0, 0, 0, 0, 0);
}
int sl_seek(int fd, uint32_t pos) {
    return (int)sys_dispatch(SYS_SEEK, (uint64_t)fd, pos, 0, 0, 0, 0);
}
int sl_tell(int fd) {
    return (int)sys_dispatch(SYS_TELL, (uint64_t)fd, 0, 0, 0, 0, 0);
}
int sl_mkdir(const char *path) {
    return (int)sys_dispatch(SYS_MKDIR, P(path), 0, 0, 0, 0, 0);
}
int sl_unlink(const char *path) {
    return (int)sys_dispatch(SYS_UNLINK, P(path), 0, 0, 0, 0, 0);
}
int sl_rename(const char *oldp, const char *newp) {
    return (int)sys_dispatch(SYS_RENAME, P(oldp), P(newp), 0, 0, 0, 0);
}
int sl_readdir(int fd, int idx, sl_dirent_t *out) {
    return (int)sys_dispatch(SYS_READDIR, (uint64_t)fd, (uint64_t)idx, P(out), 0, 0, 0);
}
int sl_stat(const char *path, sl_stat_t *out) {
    return (int)sys_dispatch(SYS_STAT, P(path), P(out), 0, 0, 0, 0);
}
int sl_sync(void) {
    return (int)sys_dispatch(SYS_SYNC, 0, 0, 0, 0, 0, 0);
}
uint64_t sl_uptime_ms(void) {
    return (uint64_t)sys_dispatch(SYS_UPTIME_MS, 0, 0, 0, 0, 0, 0);
}
void sl_sleep_ms(uint32_t ms) {
    sys_dispatch(SYS_SLEEP_MS, ms, 0, 0, 0, 0, 0);
}
void sl_yield(void) {
    sys_dispatch(SYS_YIELD, 0, 0, 0, 0, 0, 0);
}
int sl_tid(void) {
    return (int)sys_dispatch(SYS_TID, 0, 0, 0, 0, 0, 0);
}
int sl_task_name(char *buf, uint32_t sz) {
    return (int)sys_dispatch(SYS_TASKNAME, P(buf), sz, 0, 0, 0, 0);
}
int sl_datetime(sl_datetime_t *out) {
    return (int)sys_dispatch(SYS_DATETIME, P(out), 0, 0, 0, 0, 0);
}
int sl_sysstat(int idx, sl_sysstat_t *out) {
    return (int)sys_dispatch(SYS_STATS, (uint64_t)idx, P(out), 0, 0, 0, 0);
}
