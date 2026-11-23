#ifndef SYSLIB_H
#define SYSLIB_H

#include "syscall.h"

/* Application-facing system library. Every call crosses the ABI boundary
   (sys_dispatch); when user mode lands, these bodies become soft interrupts
   and nothing above this header changes. */

int  sl_open(const char *path, uint32_t flags);
int  sl_read(int fd, void *buf, uint32_t len);
int  sl_write(int fd, const void *buf, uint32_t len);
int  sl_close(int fd);
int  sl_seek(int fd, uint32_t pos);
int  sl_tell(int fd);
int  sl_mkdir(const char *path);
int  sl_unlink(const char *path);
int  sl_rename(const char *oldp, const char *newp);
int  sl_readdir(int fd, int idx, sl_dirent_t *out);
int  sl_stat(const char *path, sl_stat_t *out);
int  sl_sync(void);

uint64_t sl_uptime_ms(void);
void     sl_sleep_ms(uint32_t ms);
void     sl_yield(void);
int      sl_tid(void);
int      sl_task_name(char *buf, uint32_t sz);
int      sl_datetime(sl_datetime_t *out);
int      sl_sysstat(int idx, sl_sysstat_t *out);

int  sl_kprint(const char *s);
void sl_exit(int code);
#endif
