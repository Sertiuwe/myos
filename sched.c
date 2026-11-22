#include "sched.h"
#include "pit.h"
#include "memory.h"
#include "kstring.h"
#include "kstdio.h"

extern void ctx_switch(uint64_t *save_rsp, uint64_t load_rsp);

static thread_t  threads[SCHED_MAX_THREADS];
static thread_t *current;
static uint64_t  boot_tick;
static uint32_t  norm_counter;

static void cli(void) { __asm__ volatile("cli"); }
static void sti(void) { __asm__ volatile("sti"); }

/* ---------------- MLFQ effective priority ---------------- */

static int eff_prio(const thread_t *t) {
    int p = t->base_prio;
    if (t->wake_boost_ticks > 0 && p > 0) p--;   /* interactive wake boost */
    if (t->mtx_boost < p) p = t->mtx_boost;      /* priority inheritance */
    if (p < 0) p = 0;
    if (p > PRIO_IDLE) p = PRIO_IDLE;
    return p;
}

int sched_eff_prio_tid(int tid) {
    if (tid < 0 || tid >= SCHED_MAX_THREADS || !threads[tid].used) return -1;
    return eff_prio(&threads[tid]);
}

static thread_t *ring_next(thread_t *t) {
    int i = (int)(t - threads);
    for (int k = 1; k <= SCHED_MAX_THREADS; k++) {
        thread_t *c = &threads[(i + k) % SCHED_MAX_THREADS];
        if (c->used) return c;
    }
    return t;
}

/* ---------------- core switch ---------------- */

static void pick_and_switch(void) {
    thread_t *best = NULL;
    int best_p = 999;
    thread_t *t = current;
    /* ring order + strict '<' => round-robin inside the same priority */
    for (int k = 1; k <= SCHED_MAX_THREADS; k++) {
        t = ring_next(t);
        if (t->state != T_READY) continue;
        int p = eff_prio(t);
        if (p < best_p) { best = t; best_p = p; }
    }
    if (!best) {
        if (current->state == T_DEAD) {
            /* Nothing runnable: park with IF=1 so PIT ticks keep arriving
               and wake_expired() can ready a sleeper on the next tick. */
            for (;;) __asm__ volatile("sti; hlt");
        }
        return;
    }
    if (best == current) return;
    if (current->state == T_RUNNING) { current->state = T_READY; current->preempts++; }
    best->state = T_RUNNING;
    thread_t *prev = current;
    current = best;
    current->slice_start = pit_ticks();
    ctx_switch(&prev->rsp, best->rsp);
}

/* wake-preemption: a higher-priority READY thread runs immediately */
static void preempt_if_better(void) {
    if (current->preempt_off > 0) return;
    if (current->state != T_RUNNING) return;
    int cp = eff_prio(current);
    thread_t *t = current;
    for (int k = 1; k <= SCHED_MAX_THREADS; k++) {
        t = ring_next(t);
        if (t->state == T_READY && eff_prio(t) < cp) { pick_and_switch(); return; }
    }
}

static void wake_expired(void) {
    uint64_t now = pit_ticks();
    for (int i = 0; i < SCHED_MAX_THREADS; i++) {
        thread_t *t = &threads[i];
        if (!t->used) continue;
        if (t->wake_boost_ticks > 0) t->wake_boost_ticks--;
        if (t->state == T_BLOCKED && !t->chan && t->wake_tick && now >= t->wake_tick) {
            t->state = T_READY;
            t->wake_tick = 0;
            t->wake_boost_ticks = WAKE_BOOST_TICKS;
            t->full_slices = 0;
            t->wakes++;
        }
    }
    /* periodic regression to static base: starvation is impossible */
    if (++norm_counter >= 1000) {
        norm_counter = 0;
        for (int i = 0; i < SCHED_MAX_THREADS; i++)
            if (threads[i].used) threads[i].base_prio = threads[i].spawn_prio;
    }
}

void sched_tick(void) {
    if (!current) return;
    current->cpu_ticks++;
    wake_expired();
    if (current->preempt_off > 0) return;

    uint64_t now = pit_ticks();
    if (now - current->slice_start >= SCHED_SLICE_TICKS) {
        current->slice_start = now;
        current->full_slices++;
        /* CPU-bound detection: two full slices in a row => demote */
        if (current->full_slices >= 2 && current->base_prio < PRIO_IDLE) {
            current->base_prio++;
            current->demotes++;
            current->full_slices = 0;
        }
        pick_and_switch();
        return;
    }
    preempt_if_better();
}

static void thread_tramp(void) {
    sti();
    current->entry(current->arg);
    sched_exit();
}

/* ---------------- lifecycle ---------------- */

void sched_init(void) {
    memset(threads, 0, sizeof(threads));
    thread_t *t = &threads[0];
    t->used = 1;
    t->id = 0;
    t->state = T_RUNNING;
    strcpy(t->name, "main");
    t->spawn_prio = PRIO_IDLE;
    t->base_prio = PRIO_IDLE;
    t->mtx_boost = MTX_NO_BOOST;
    t->slice_start = pit_ticks();
    current = t;
    boot_tick = pit_ticks();
    kprintf("SCHED: MLFQ %d levels, slice=%u ticks @ %u Hz, wake-preempt + prio inheritance\n",
            SCHED_PRIO_LEVELS, SCHED_SLICE_TICKS, pit_hz());
}

int sched_spawn(const char *name, void (*entry)(void *), void *arg, uint64_t stack_size) {
    if (!stack_size) stack_size = 16384;
    cli();
    thread_t *t = NULL;
    for (int i = 1; i < SCHED_MAX_THREADS; i++)
        if (!threads[i].used) { t = &threads[i]; break; }
    if (!t) {
        for (int i = 1; i < SCHED_MAX_THREADS; i++)
            if (threads[i].used && threads[i].state == T_DEAD) { t = &threads[i]; break; }
        if (t && t->stack_base && t->stack_size) {
            kfree((void *)(uintptr_t)t->stack_base);
            t->stack_base = 0;
        }
    }
    if (!t) { sti(); return -1; }
    memset(t, 0, sizeof(*t));
    t->used = 1;
    t->id = (int)(t - threads);
    t->state = T_READY;
    t->entry = entry;
    t->arg = arg;
    t->stack_size = stack_size;
    t->spawn_prio = PRIO_NORM;
    t->base_prio = PRIO_NORM;
    t->mtx_boost = MTX_NO_BOOST;
    t->slice_start = pit_ticks();
    strncpy(t->name, name ? name : "task", sizeof(t->name) - 1);

    uint8_t *mem = kmalloc(stack_size + 64);
    if (!mem) { t->used = 0; sti(); return -1; }
    t->stack_base = (uint64_t)(uintptr_t)mem;
    uint64_t top = (t->stack_base + stack_size) & ~(uint64_t)15;
    uint64_t *sp = (uint64_t *)top;
    *--sp = (uint64_t)(uintptr_t)thread_tramp;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    t->rsp = (uint64_t)(uintptr_t)sp;
    sti();
    return t->id;
}

void sched_yield(void) {
    cli();
    current->full_slices = 0;
    pick_and_switch();
    sti();
}

void sched_exit(void) {
    cli();
    current->state = T_DEAD;
    pick_and_switch();
    for (;;) __asm__ volatile("sti; hlt");
}

void sched_sleep_ms(uint32_t ms) {
    uint32_t hz = pit_hz();
    cli();
    current->wake_tick = pit_ticks() + (uint64_t)ms * hz / 1000 + 1;
    current->chan = NULL;
    current->state = T_BLOCKED;
    current->full_slices = 0;
    pick_and_switch();
    sti();
}

void sched_block_on(void *chan) {
    cli();
    current->chan = chan;
    current->wake_tick = 0;
    current->state = T_BLOCKED;
    current->full_slices = 0;
    pick_and_switch();
    sti();
}

void sched_wake(void *chan) {
    cli();
    for (int i = 0; i < SCHED_MAX_THREADS; i++) {
        thread_t *t = &threads[i];
        if (t->used && t->state == T_BLOCKED && t->chan == chan) {
            t->state = T_READY;
            t->chan = NULL;
            t->wake_boost_ticks = WAKE_BOOST_TICKS;
            t->full_slices = 0;
            t->wakes++;
        }
    }
    preempt_if_better();
    sti();
}

void sched_wake_tid(int tid) {
    if (tid < 0 || tid >= SCHED_MAX_THREADS) return;
    cli();
    thread_t *t = &threads[tid];
    if (t->used && t->state == T_BLOCKED) {
        t->state = T_READY;
        t->chan = NULL;
        t->wake_tick = 0;
        t->wake_boost_ticks = WAKE_BOOST_TICKS;
        t->full_slices = 0;
        t->wakes++;
    }
    preempt_if_better();
    sti();
}

void sched_set_base_prio(int tid, int prio) {
    if (tid < 0 || tid >= SCHED_MAX_THREADS) return;
    if (prio < 0) prio = 0;
    if (prio > PRIO_IDLE) prio = PRIO_IDLE;
    cli();
    threads[tid].spawn_prio = prio;
    threads[tid].base_prio = prio;
    sti();
}

int sched_current_id(void) { return current ? current->id : -1; }
const char *sched_current_name(void) { return current ? current->name : "?"; }

int sched_count(void) {
    int n = 0;
    for (int i = 0; i < SCHED_MAX_THREADS; i++) if (threads[i].used) n++;
    return n;
}

const thread_t *sched_get(int idx) {
    if (idx < 0 || idx >= SCHED_MAX_THREADS) return NULL;
    return threads[idx].used ? &threads[idx] : NULL;
}

void sched_preempt_disable(void) {
    cli();
    current->preempt_off++;
    sti();
}

void sched_preempt_enable(void) {
    cli();
    if (current->preempt_off > 0) current->preempt_off--;
    sti();
}

/* ---------------- mutex with priority inheritance ---------------- */

void kmutex_init(kmutex_t *m) {
    m->locked = 0;
    m->owner_id = -1;
    m->holder = -1;
    m->wait_max = MTX_NO_BOOST;
}

void kmutex_lock(kmutex_t *m) {
    sched_preempt_disable();
    for (int spins = 0;; spins++) {
        if (__sync_val_compare_and_swap(&m->locked, 0, 1) == 0) {
            m->owner_id = sched_current_id();
            m->holder = sched_current_id();
            m->wait_max = MTX_NO_BOOST;
            return;
        }
        /* inheritance: boost the holder up to the most important waiter */
        int my = eff_prio(current);
        if (my < m->wait_max) m->wait_max = my;
        if (m->holder >= 0 && m->holder < SCHED_MAX_THREADS &&
            m->wait_max < threads[m->holder].mtx_boost)
            threads[m->holder].mtx_boost = m->wait_max;
        if (spins < 300) { __asm__ volatile("pause"); continue; }
        sched_yield();
        spins = 0;
    }
}

void kmutex_unlock(kmutex_t *m) {
    int h = m->holder;
    m->owner_id = -1;
    m->holder = -1;
    m->wait_max = MTX_NO_BOOST;
    if (h >= 0 && h < SCHED_MAX_THREADS) threads[h].mtx_boost = MTX_NO_BOOST;
    __sync_lock_release(&m->locked);
    sched_preempt_enable();
}

/* ---------------- selftest ---------------- */

static volatile int st_counter;
static volatile int st_done;

static void st_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 150; i++) {
        st_counter++;
        sched_yield();
    }
    __sync_fetch_and_add(&st_done, 1);
    sched_exit();
}

static volatile int st_hi_ran_first;
static void st_hi(void *arg) { (void)arg; st_hi_ran_first = (st_counter == 0); sched_exit(); }
static void st_bg(void *arg) { (void)arg; st_counter = 999; sched_exit(); }

void sched_selftest(void) {
    st_counter = 0;
    st_done = 0;
    kprintf("--- SCHED SELFTEST ---\n");
    for (int i = 0; i < 3; i++) sched_spawn("st_worker", st_worker, NULL, 8192);
    int spins = 0;
    while (st_done < 3 && spins < 200000000) { sched_yield(); spins++; }
    kprintf("3 workers x 150 yields: counter=%d (expect 450): %s\n",
            st_counter, st_counter == 450 ? "PASS" : "FAIL");
    kprintf("workers exited: %s\n", st_done == 3 ? "PASS" : "FAIL");

    /* wake-preemption: HI must run before BG even though BG spawned first */
    st_counter = 0;
    st_hi_ran_first = 0;
    int bg = sched_spawn("st_bg", st_bg, NULL, 8192);
    int hi = sched_spawn("st_hi", st_hi, NULL, 8192);
    sched_set_base_prio(bg, PRIO_BG);
    sched_set_base_prio(hi, PRIO_HI);
    sched_sleep_ms(30);
    kprintf("wake-preempt HI before BG: %s\n", st_hi_ran_first ? "PASS" : "FAIL");

    kprintf("sleep 50ms drift: ");
    uint64_t t0 = pit_ticks();
    sched_sleep_ms(50);
    uint64_t dt = pit_ticks() - t0;
    uint64_t want = (uint64_t)50 * pit_hz() / 1000;
    uint64_t lo = want - want / 5, hi2 = want + want / 5;
    kprintf("%u ticks (expect ~%u): %s\n",
            (unsigned)dt, (unsigned)want, (dt >= lo && dt <= hi2) ? "PASS" : "FAIL");
    kprintf("--- SELFTEST DONE ---\n");
}
