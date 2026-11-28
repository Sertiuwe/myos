#include "syslib.h"

/* Every call now crosses the real gate. From ring 0 the CPU keeps the current
   stack; from ring 3 it switches to TSS.RSP0. Same ABI either way. */
static inline int64_t sys_call6(uint64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    int64_t ret;
    register uint64_t r8  __asm__("r8")  = a4;
    register uint64_t r9  __asm__("r9")  = a5;
    register uint64_t r10 __asm__("r10") = a6;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "S"(a1), "d"(a2), "c"(a3),
                       "r"(r8), "r"(r9), "r"(r10)
                     : "memory");
    return ret;
}

static uint64_t P(const void *p) { return (uint64_t)(uintptr_t)p; }

int sl_open(const char *path, uint32_t flags) {
    return (int)sys_call6(SYS_OPEN, P(path), flags, 0, 0, 0, 0);
}
int sl_read(int fd, void *buf, uint32_t len) {
    return (int)sys_call6(SYS_READ, (uint64_t)fd, P(buf), len, 0, 0, 0);
}
int sl_write(int fd, const void *buf, uint32_t len) {
    return (int)sys_call6(SYS_WRITE, (uint64_t)fd, P(buf), len, 0, 0, 0);
}
int sl_close(int fd) { return (int)sys_call6(SYS_CLOSE, (uint64_t)fd, 0, 0, 0, 0, 0); }
int sl_seek(int fd, uint32_t pos) { return (int)sys_call6(SYS_SEEK, (uint64_t)fd, pos, 0, 0, 0, 0); }
int sl_tell(int fd) { return (int)sys_call6(SYS_TELL, (uint64_t)fd, 0, 0, 0, 0, 0); }
int sl_mkdir(const char *path) { return (int)sys_call6(SYS_MKDIR, P(path), 0, 0, 0, 0, 0); }
int sl_unlink(const char *path) { return (int)sys_call6(SYS_UNLINK, P(path), 0, 0, 0, 0, 0); }
int sl_rename(const char *o, const char *n) { return (int)sys_call6(SYS_RENAME, P(o), P(n), 0, 0, 0, 0); }
int sl_readdir(int fd, int idx, sl_dirent_t *out) {
    return (int)sys_call6(SYS_READDIR, (uint64_t)fd, (uint64_t)idx, P(out), 0, 0, 0);
}
int sl_stat(const char *path, sl_stat_t *out) { return (int)sys_call6(SYS_STAT, P(path), P(out), 0, 0, 0, 0); }
int sl_sync(void) { return (int)sys_call6(SYS_SYNC, 0, 0, 0, 0, 0, 0); }
uint64_t sl_uptime_ms(void) { return (uint64_t)sys_call6(SYS_UPTIME_MS, 0, 0, 0, 0, 0, 0); }
void sl_sleep_ms(uint32_t ms) { sys_call6(SYS_SLEEP_MS, ms, 0, 0, 0, 0, 0); }
void sl_yield(void) { sys_call6(SYS_YIELD, 0, 0, 0, 0, 0, 0); }
int sl_tid(void) { return (int)sys_call6(SYS_TID, 0, 0, 0, 0, 0, 0); }
int sl_task_name(char *buf, uint32_t sz) { return (int)sys_call6(SYS_TASKNAME, P(buf), sz, 0, 0, 0, 0); }
int sl_datetime(sl_datetime_t *out) { return (int)sys_call6(SYS_DATETIME, P(out), 0, 0, 0, 0, 0); }
int sl_sysstat(int idx, sl_sysstat_t *out) { return (int)sys_call6(SYS_STATS, (uint64_t)idx, P(out), 0, 0, 0, 0); }
int sl_kprint(const char *s) { return (int)sys_call6(SYS_KPRINT, P(s), 0, 0, 0, 0, 0); }
void sl_exit(int code) { sys_call6(SYS_EXIT, (uint64_t)code, 0, 0, 0, 0, 0); }
