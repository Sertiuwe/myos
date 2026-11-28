#include "sched.h"
#include "pit.h"
#include "memory.h"
#include "kstring.h"
#include "kstdio.h"

extern void ctx_switch(uint64_t *save_rsp, uint64_t load_rsp);

#define NLVL 32
#define WAKE_BOOST   2      /* NT interactive wake boost */
#define FG_BOOST     2      /* NT foreground boost */
#define STARVE_TICKS 3000   /* ~3 s not run => balance-set boost */

static thread_t  threads[SCHED_MAX_THREADS];
static thread_t *current;
static uint64_t  boot_tick;
static uint32_t  scan_counter;
static void (*tss_updater)(uint64_t);

/* per-level ready rings + bitmap of non-empty levels */
static int      rq_head[NLVL], rq_tail[NLVL];
static uint32_t rq_mask;

static void cli(void) { __asm__ volatile("cli"); }
static void sti(void) { __asm__ volatile("sti"); }

/* ---------------- ready queues ---------------- */
static void rq_add(thread_t *t, int lvl) {
    if (lvl < 0) lvl = 0; if (lvl > 31) lvl = 31;
    t->rq_level = lvl; t->rq_next = -1; t->in_rq = 1;
    int tl = rq_tail[lvl];
    if (tl >= 0) { threads[tl].rq_next = t->id; t->rq_prev = tl; }
    else { rq_head[lvl] = t->id; t->rq_prev = -1; }
    rq_tail[lvl] = t->id;
    rq_mask |= (1u << lvl);
}
static void rq_remove(thread_t *t) {
    if (!t->in_rq) return;
    int lvl = t->rq_level;
    int pv = t->rq_prev, nx = t->rq_next;
    if (pv >= 0) threads[pv].rq_next = nx; else rq_head[lvl] = nx;
    if (nx >= 0) threads[nx].rq_prev = pv; else rq_tail[lvl] = pv;
    if (rq_head[lvl] < 0) rq_mask &= ~(1u << lvl);
    t->in_rq = 0;
}
static thread_t *rq_pick(void) {
    if (!rq_mask) return NULL;
    int lvl = 31 - __builtin_clz(rq_mask);
    return &threads[rq_head[lvl]];
}

/* ---------------- effective priority ---------------- */
static int eff_prio(const thread_t *t) {
    int p = t->cur_prio;
    if (t->mtx_boost != MTX_NO_BOOST && t->mtx_boost > p) p = t->mtx_boost;  /* inheritance */
    if (p < 0) p = 0; if (p > 31) p = 31;
    return p;
}
int sched_eff_prio_tid(int tid) {
    if (tid < 0 || tid >= SCHED_MAX_THREADS || !threads[tid].used) return -1;
    return eff_prio(&threads[tid]);
}
static int wake_prio(const thread_t *t) {
    if (t->base_prio >= 16) return t->base_prio;          /* real-time: no boost */
    int p = t->base_prio + WAKE_BOOST;
    if (p > 15) p = 15;                                    /* dynamic band cap */
    return p;
}
static int quantum_for(const thread_t *t) {
    int q = SCHED_SLICE_TICKS;
    if (t->fg) q *= 3;                                     /* NT foreground quantum */
    if (t->base_prio >= 16) q *= 2;
    return q;
}

/* ---------------- core switch ---------------- */
static void pick_and_switch(void) {
    thread_t *best = rq_pick();
    if (!best) {
        if (current->state == T_DEAD) for (;;) __asm__ volatile("sti; hlt");
        return;
    }
    if (best == current) {                                 /* round-robin landed on us */
        rq_remove(best); best->state = T_RUNNING;
        best->quantum_left = quantum_for(best);
        best->slice_start = pit_ticks();
        return;
    }
    if (current->state == T_RUNNING && best->rq_level <= eff_prio(current)) return;
    thread_t *prev = current;
    if (prev->state == T_RUNNING) { prev->state = T_READY; rq_add(prev, eff_prio(prev)); prev->preempts++; }
    rq_remove(best);
    best->state = T_RUNNING;
    current = best;
    best->last_run = pit_ticks();
    best->quantum_left = quantum_for(best);
    best->slice_start = pit_ticks();
    if (tss_updater) {
        uint64_t top = best->stack_size ? (best->stack_base + best->stack_size) : 0;
        tss_updater(top);
    }
    ctx_switch(&prev->rsp, best->rsp);
}

static void preempt_if_better(void) {
    if (current->preempt_off > 0) return;
    if (current->state != T_RUNNING) return;
    thread_t *best = rq_pick();
    if (best && best->rq_level > eff_prio(current)) pick_and_switch();
}

/* ---------------- periodic: wake timers + starvation scan ---------------- */
static void wake_expired(void) {
    uint64_t now = pit_ticks();
    for (int i = 0; i < SCHED_MAX_THREADS; i++) {
        thread_t *t = &threads[i];
        if (!t->used) continue;
        if (t->state == T_BLOCKED && t->wake_tick && now >= t->wake_tick) {
            rq_remove(t);
            t->state = T_READY;
            t->timed_out = (t->chan != NULL);
            t->chan = NULL; t->wake_tick = 0;
            t->cur_prio = wake_prio(t);
            rq_add(t, t->cur_prio);
            t->wakes++;
        }
    }
    /* balance-set manager: boost starved ready threads */
    if (++scan_counter >= 1000) {
        scan_counter = 0;
        for (int i = 0; i < SCHED_MAX_THREADS; i++) {
            thread_t *t = &threads[i];
            if (!t->used || t->state != T_READY || !t->in_rq) continue;
            if (t->base_prio >= 16) continue;
            if (now - t->last_run > STARVE_TICKS) {
                rq_remove(t);
                t->cur_prio = 15;
                rq_add(t, 15);
                t->boosts++;
            }
        }
    }
}

void sched_tick(void) {
    if (!current) return;
    current->cpu_ticks++;
    wake_expired();
    if (current->preempt_off > 0) return;
    if (--current->quantum_left <= 0) {
        /* quantum end: reset boosts toward base; CPU-bound demote in dynamic band */
        if (current->base_prio < 16) {
            /* NT: quantum end only clears temporary boosts; base is untouched */
            current->full_slices++;
            current->cur_prio = current->base_prio;
        }
        current->state = T_READY;
        rq_add(current, eff_prio(current));
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
    for (int i = 0; i < NLVL; i++) { rq_head[i] = -1; rq_tail[i] = -1; }
    rq_mask = 0;
    thread_t *t = &threads[0];
    t->used = 1; t->id = 0; t->state = T_RUNNING;
    strcpy(t->name, "main");
    t->spawn_prio = PRIO_IDLE; t->base_prio = PRIO_IDLE; t->cur_prio = PRIO_IDLE;
    t->mtx_boost = MTX_NO_BOOST;
    t->last_run = pit_ticks(); t->slice_start = pit_ticks();
    t->quantum_left = quantum_for(t);
    current = t;
    boot_tick = pit_ticks();
    kprintf("SCHED: NT-like %d levels, slice=%u ticks @ %u Hz, fg-boost + PI + starvation scan\n",
            NLVL, SCHED_SLICE_TICKS, pit_hz());
}

int sched_spawn(const char *name, void (*entry)(void *), void *arg, uint64_t stack_size) {
    if (!stack_size) stack_size = 16384;
    cli();
    thread_t *t = NULL;
    for (int i = 1; i < SCHED_MAX_THREADS; i++) if (!threads[i].used) { t = &threads[i]; break; }
    if (!t) {
        for (int i = 1; i < SCHED_MAX_THREADS; i++)
            if (threads[i].used && threads[i].state == T_DEAD) { t = &threads[i]; break; }
        if (t && t->stack_base && t->stack_size) { kfree((void *)(uintptr_t)t->stack_base); t->stack_base = 0; }
    }
    if (!t) { sti(); return -1; }
    memset(t, 0, sizeof(*t));
    t->used = 1; t->id = (int)(t - threads);
    t->state = T_READY; t->entry = entry; t->arg = arg; t->stack_size = stack_size;
    t->spawn_prio = PRIO_NORM; t->base_prio = PRIO_NORM; t->cur_prio = PRIO_NORM;
    t->mtx_boost = MTX_NO_BOOST;
    t->last_run = pit_ticks(); t->slice_start = pit_ticks();
    t->quantum_left = quantum_for(t);
    strncpy(t->name, name ? name : "task", sizeof(t->name) - 1);
    uint8_t *mem = kmalloc(stack_size + 64);
    if (!mem) { t->used = 0; sti(); return -1; }
    t->stack_base = (uint64_t)(uintptr_t)mem;
    uint64_t top = (t->stack_base + stack_size) & ~(uint64_t)15;
    uint64_t *sp = (uint64_t *)top;
    *--sp = (uint64_t)(uintptr_t)thread_tramp;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    t->rsp = (uint64_t)(uintptr_t)sp;
    rq_add(t, t->cur_prio);
    sti();
    preempt_if_better();
    return t->id;
}

void sched_yield(void) {
    cli();
    current->full_slices = 0;
    current->state = T_READY;
    rq_add(current, eff_prio(current));
    pick_and_switch();
    sti();
}

void sched_exit(void) {
    cli();
    rq_remove(current);
    current->state = T_DEAD;
    pick_and_switch();
    for (;;) __asm__ volatile("sti; hlt");
}

void sched_sleep_ms(uint32_t ms) {
    uint32_t hz = pit_hz();
    cli();
    current->wake_tick = pit_ticks() + (uint64_t)ms * hz / 1000 + 1;
    current->chan = NULL;
    current->timed_out = 0;
    current->state = T_BLOCKED;
    current->full_slices = 0;
    pick_and_switch();
    sti();
}

void sched_block_on(void *chan) {
    cli();
    current->chan = chan;
    current->wake_tick = 0;
    current->timed_out = 0;
    current->state = T_BLOCKED;
    current->full_slices = 0;
    pick_and_switch();
    sti();
}

int sched_block_on_timeout(void *chan, uint32_t ms) {
    uint32_t hz = pit_hz();
    cli();
    current->chan = chan;
    current->wake_tick = pit_ticks() + (uint64_t)ms * hz / 1000 + 1;
    current->timed_out = 0;
    current->state = T_BLOCKED;
    current->full_slices = 0;
    pick_and_switch();
    sti();
    return current->timed_out ? -1 : 0;
}

static void do_wake(thread_t *t) {
    rq_remove(t);
    t->state = T_READY;
    t->chan = NULL; t->wake_tick = 0;
    t->cur_prio = wake_prio(t);
    t->full_slices = 0;
    rq_add(t, t->cur_prio);
    t->wakes++;
}
void sched_wake(void *chan) {
    cli();
    for (int i = 0; i < SCHED_MAX_THREADS; i++) {
        thread_t *t = &threads[i];
        if (t->used && t->state == T_BLOCKED && t->chan == chan) do_wake(t);
    }
    preempt_if_better();
    sti();
}
void sched_wake_tid(int tid) {
    if (tid < 0 || tid >= SCHED_MAX_THREADS) return;
    cli();
    thread_t *t = &threads[tid];
    if (t->used && t->state == T_BLOCKED) do_wake(t);
    preempt_if_better();
    sti();
}

void sched_set_base_prio(int tid, int prio) {
    if (tid < 0 || tid >= SCHED_MAX_THREADS) return;
    if (prio < 0) prio = 0; if (prio > 31) prio = 31;
    cli();
    threads[tid].spawn_prio = prio;
    threads[tid].base_prio = prio;
    if (threads[tid].state == T_READY && threads[tid].in_rq) {
        rq_remove(&threads[tid]);
        threads[tid].cur_prio = prio;
        rq_add(&threads[tid], prio);
    } else threads[tid].cur_prio = prio;
    sti();
    preempt_if_better();
}

void sched_set_foreground(int tid) {
    if (tid < 0 || tid >= SCHED_MAX_THREADS) return;
    cli();
    threads[tid].fg = 1;
    if (threads[tid].base_prio < 16) {
        int b = threads[tid].base_prio + FG_BOOST; if (b > 15) b = 15;
        threads[tid].base_prio = b;
        if (threads[tid].in_rq) { rq_remove(&threads[tid]); rq_add(&threads[tid], b); }
        threads[tid].cur_prio = b;
    }
    sti();
}

void sched_set_class(int tid, int cls, int rel) {
    int base = cls + rel;
    if (cls >= 16) { if (base < 16) base = 16; if (base > 31) base = 31; }
    else { if (base < 1) base = 1; if (base > 15) base = 15; }
    sched_set_base_prio(tid, base);
}

int sched_started(void) { return current != NULL; }
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
void sched_preempt_disable(void) { cli(); current->preempt_off++; sti(); }
void sched_preempt_enable(void) {
    cli();
    if (current->preempt_off > 0) current->preempt_off--;
    sti();
    preempt_if_better();
}

/* ---------------- mutex with priority inheritance ---------------- */
void kmutex_init(kmutex_t *m) { m->locked = 0; m->owner_id = -1; m->holder = -1; m->wait_max = MTX_NO_BOOST; }
void kmutex_lock(kmutex_t *m) {
    sched_preempt_disable();
    for (int spins = 0;; spins++) {
        if (__sync_val_compare_and_swap(&m->locked, 0, 1) == 0) {
            m->owner_id = sched_current_id(); m->holder = sched_current_id();
            m->wait_max = MTX_NO_BOOST;
            return;
        }
        int my = eff_prio(current);
        if (m->wait_max == MTX_NO_BOOST || my > m->wait_max) m->wait_max = my;
        int h = m->holder;
        if (h >= 0 && h < SCHED_MAX_THREADS && m->wait_max > threads[h].mtx_boost)
            threads[h].mtx_boost = m->wait_max;
        if (spins < 300) { __asm__ volatile("pause"); continue; }
        sched_yield();
        spins = 0;
    }
}
void kmutex_unlock(kmutex_t *m) {
    int h = m->holder;
    m->owner_id = -1; m->holder = -1; m->wait_max = MTX_NO_BOOST;
    if (h >= 0 && h < SCHED_MAX_THREADS) threads[h].mtx_boost = MTX_NO_BOOST;
    __sync_lock_release(&m->locked);
    sched_preempt_enable();
}

/* ---------------- NT-like dump ---------------- */
void sched_dump(void) {
    static const char *stn[] = { "DEAD", "READY", "RUN", "BLOCK" };
    kprintf("id  name            state  base cur eff q  fg boosts demotes preempts wakes\n");
    for (int i = 0; i < SCHED_MAX_THREADS; i++) {
        thread_t *t = &threads[i];
        if (!t->used) continue;
        kprintf("%-3d %-15s %-6s %-4d %-3d %-3d %-2d %-2d %-6u %-7u %-8u %u\n",
                t->id, t->name, stn[t->state], t->base_prio, t->cur_prio, eff_prio(t),
                t->quantum_left, t->fg, (unsigned)t->boosts, (unsigned)t->demotes,
                (unsigned)t->preempts, (unsigned)t->wakes);
    }
    kprintf("ready levels: mask=%08x\n", rq_mask);
}

/* ---------------- selftest ---------------- */
static volatile int st_counter;
static volatile int st_done;
static void st_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 150; i++) { st_counter++; sched_yield(); }
    __sync_fetch_and_add(&st_done, 1);
    sched_exit();
}
static volatile int st_hi_ran_first;
static void st_hi(void *arg) { (void)arg; st_hi_ran_first = (st_counter == 0); sched_exit(); }
static void st_bg(void *arg) { (void)arg; st_counter = 999; sched_exit(); }
static volatile int st_to;
static void st_timeout(void *arg) { (void)arg; st_to = sched_block_on_timeout((void *)0xdead, 40); sched_exit(); }

void sched_selftest(void) {
    st_counter = 0; st_done = 0;
    kprintf("--- SCHED SELFTEST ---\n");
    for (int i = 0; i < 3; i++) sched_spawn("st_worker", st_worker, NULL, 8192);
    int spins = 0;
    while (st_done < 3 && spins < 200000000) { sched_yield(); spins++; }
    kprintf("3 workers x 150 yields: counter=%d (expect 450): %s\n",
            st_counter, st_counter == 450 ? "PASS" : "FAIL");
    kprintf("workers exited: %s\n", st_done == 3 ? "PASS" : "FAIL");

    st_counter = 0; st_hi_ran_first = 0;
    int bg = sched_spawn("st_bg", st_bg, NULL, 8192);
    int hi = sched_spawn("st_hi", st_hi, NULL, 8192);
    sched_set_base_prio(bg, PRIO_BG);
    sched_set_base_prio(hi, PRIO_HI);
    sched_sleep_ms(30);
    kprintf("wake-preempt HI before BG: %s\n", st_hi_ran_first ? "PASS" : "FAIL");

    st_to = 1;
    sched_spawn("st_timeout", st_timeout, NULL, 8192);
    uint64_t t0 = pit_ticks();
    int spins2 = 0;
    while (st_to == 1 && spins2 < 200000000) { sched_yield(); spins2++; }
    uint64_t dt = pit_ticks() - t0;
    kprintf("block_on_timeout 40ms: rc=%d dt=%u ticks: %s\n",
            st_to, (unsigned)dt, (st_to == -1) ? "PASS" : "FAIL");

    kprintf("sleep 50ms drift: ");
    uint64_t t1 = pit_ticks();
    sched_sleep_ms(50);
    uint64_t d2 = pit_ticks() - t1;
    uint64_t want = (uint64_t)50 * pit_hz() / 1000;
    uint64_t lo = want - want / 5, hi2 = want + want / 5;
    kprintf("%u ticks (expect ~%u): %s\n",
            (unsigned)d2, (unsigned)want, (d2 >= lo && d2 <= hi2) ? "PASS" : "FAIL");
    kprintf("--- SELFTEST DONE ---\n");
}

void sched_set_tss_updater(void (*fn)(uint64_t rsp0)) { tss_updater = fn; }
