#include "fslock.h"
#include "sched.h"

static kmutex_t fs_mtx;
static int fs_mtx_ready;

static void fs_lock_init(void) {
    if (!fs_mtx_ready) { kmutex_init(&fs_mtx); fs_mtx_ready = 1; }
}

void fs_lock(void)   { fs_lock_init(); kmutex_lock(&fs_mtx); }
void fs_unlock(void) { kmutex_unlock(&fs_mtx); }
