#include "sxnl.h"
#include "kstdio.h"
#include "kstring.h"
#include "sched.h"
#include "pit.h"

#define SXNL_LOG      64
#define SXNL_MAX_FAULTS 3      /* per-thread fault budget before forced kill */

#ifndef SXNL_USER_BASE
#define SXNL_USER_BASE  0x2000000ULL
#endif
#ifndef SXNL_USER_LIMIT
#define SXNL_USER_LIMIT 0x4000000ULL
#endif

typedef struct {
    uint64_t tick;
    int      tid;
    uint8_t  kind;
    uint64_t vec;
    uint64_t addr;
    char     name[24];
} sxnl_ev_t;

static sxnl_ev_t log[SXNL_LOG];
static int       log_pos, log_count;
static uint32_t  faults[SCHED_MAX_THREADS];

void sxnl_init(void) {
    memset(log, 0, sizeof(log));
    memset(faults, 0, sizeof(faults));
    log_pos = 0; log_count = 0;
    kprintf("SXNL: security subsystem online (fault policy: kill ring3 offender, panic on kernel fault)\n");
}

static void push(uint8_t kind, uint64_t vec, uint64_t addr) {
    sxnl_ev_t *e = &log[log_pos % SXNL_LOG];
    e->tick = pit_ticks();
    e->tid  = sched_current_id();
    e->kind = kind;
    e->vec  = vec;
    e->addr = addr;
    const char *n = sched_current_name();
    strncpy(e->name, n ? n : "?", sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    log_pos++;
    if (log_count < SXNL_LOG) log_count++;
}

int sxnl_user_ok(const void *p, uint32_t len) {
    uintptr_t a = (uintptr_t)p;
    if (len == 0) return 0;
    uintptr_t b = a + len;
    if (b < a) return 0;                       /* wrap */
    if (a < SXNL_USER_BASE || b > SXNL_USER_LIMIT) return 0;
    return 1;
}

int sxnl_fault(uint64_t vec, uint64_t addr, int user_mode) {
    int tid = sched_current_id();
    push(SXNL_EV_FAULT, vec, addr);
    if (!user_mode) return SXNL_ACT_PANIC;     /* kernel fault: full stop with report */
    if (tid >= 0 && tid < SCHED_MAX_THREADS) faults[tid]++;
    push(SXNL_EV_KILL, vec, addr);
    return SXNL_ACT_KILL;                      /* ring3 fault: kill only the offender */
}

void sxnl_badptr(uint64_t ptr) {
    push(SXNL_EV_BADPTR, 0, ptr);
}

void sxnl_dump(void) {
    static const char *kn[] = { "?", "fault", "badptr", "kill", "wx" };
    kprintf("SXNL audit: %d event(s), fault budget=%u\n", log_count, SXNL_MAX_FAULTS);
    int start = log_pos > SXNL_LOG ? log_pos - SXNL_LOG : 0;
    for (int i = start; i < log_pos; i++) {
        sxnl_ev_t *e = &log[i % SXNL_LOG];
        kprintf("  t=%-8u tid=%-3d %-12s %-6s vec=%-3u addr=%p\n",
                (unsigned)e->tick, e->tid, e->name,
                kn[e->kind < 4 ? e->kind : 0], (unsigned)e->vec, (void *)e->addr);
    }
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
        if (faults[i]) kprintf("  tid %d faults=%u\n", i, faults[i]);
}

void sxnl_selftest(void) {
    kprintf("--- SXNL SELFTEST ---\n");
    int ok = 1;
    ok &= sxnl_user_ok((void *)SXNL_USER_BASE, 16);
    ok &= !sxnl_user_ok((void *)(SXNL_USER_BASE - 1), 16);
    ok &= !sxnl_user_ok((void *)(SXNL_USER_LIMIT - 8), 16);
    ok &= !sxnl_user_ok((void *)0x1000, 16);
    ok &= !sxnl_user_ok((void *)0xFFFFFFFF0000ULL, 16);
    ok &= !sxnl_user_ok((void *)SXNL_USER_BASE, 0);
    kprintf("user pointer validation: %s\n", ok ? "PASS" : "FAIL");
    kprintf("--- SELFTEST DONE ---\n");
}
