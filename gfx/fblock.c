#include "fblock.h"
#include "sched.h"

static kmutex_t m; static int ready;
static volatile int owner = -1;
static int depth;

void fb_lock(void) {
    if (!sched_started()) return;              /* boot: single thread */
    int me = sched_current_id();
    if (owner == me) { depth++; return; }      /* recursive: same thread */
    if (!ready) { kmutex_init(&m); ready = 1; }
    kmutex_lock(&m);
    owner = me; depth = 1;
}
void fb_unlock(void) {
    if (!sched_started()) return;
    if (owner == sched_current_id() && depth > 1) { depth--; return; }
    owner = -1; depth = 0;
    kmutex_unlock(&m);
}
