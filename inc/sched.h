#ifndef SCHED_H
#define SCHED_H
#include <stdint.h>

#define SCHED_MAX_THREADS   24
#define SCHED_SLICE_TICKS   20      /* 20 ms @ 1000 Hz */
#define SCHED_PRIO_LEVELS   32
/* NT-style numbering: HIGHER = more important, 0..31 */
#define PRIO_IDLE 0                 /* idle task */
#define PRIO_BG   6                 /* background workers */
#define PRIO_NORM 8                 /* app windows, shell (NT Normal) */
#define PRIO_HI   13                /* compositor, latency-critical (NT High) */
#define PRIO_RT   24                /* real-time band 16..31 */
#define MTX_NO_BOOST (-1)
/* NT priority classes (base) */
#define PCLASS_IDLE 4
#define PCLASS_BELOWNORMAL 6
#define PCLASS_NORMAL 8
#define PCLASS_ABOVENORMAL 10
#define PCLASS_HIGH 13
#define PCLASS_REALTIME 24
#define WAKE_BOOST_TICKS 5          /* interactive boost window after a wake */

typedef enum { T_DEAD = 0, T_READY, T_RUNNING, T_BLOCKED } tstate_t;

typedef struct thread thread_t;
struct thread {
    int         id;
    char        name[24];
    tstate_t    state;
    uint64_t    rsp;
    uint64_t    stack_base;
    uint64_t    stack_size;
    void      (*entry)(void *);
    void       *arg;
    uint64_t    wake_tick;
    void       *chan;
    uint64_t    cpu_ticks;
    int         used;
    int         preempt_off;
    /* MLFQ */
    int         spawn_prio;         /* static base, restored periodically */
    int         base_prio;          /* dynamic base, boosted/demoted */
    int         mtx_boost;          /* priority inheritance while holding a lock */
    int         wake_boost_ticks;   /* interactive boost countdown */
    int         rq_next, rq_prev, rq_level, in_rq;
    int         cur_prio;           /* current dynamic level (queued at) */
    int         quantum_left;
    int         fg;                 /* NT foreground flag */
    int         timed_out;
    uint64_t    last_run;
    uint64_t    slice_start;
    uint64_t    full_slices;
    uint64_t    boosts, demotes, preempts, wakes;
};

typedef struct {
    volatile int locked;
    volatile int owner_id;
    int          holder;
    int          wait_max;
} kmutex_t;

void sched_init(void);
void sched_selftest(void);
int  sched_spawn(const char *name, void (*entry)(void *), void *arg, uint64_t stack_size);
void sched_yield(void);
void sched_exit(void);
void sched_sleep_ms(uint32_t ms);
void sched_block_on(void *chan);
int  sched_block_on_timeout(void *chan, uint32_t ms);   /* 0 = woken, -1 = timeout */
void sched_set_foreground(int tid);                     /* NT foreground boost */
void sched_set_class(int tid, int cls, int rel);        /* class x relative -> base */
void sched_dump(void);                                  /* NT-like scheduler table */
void sched_wake(void *chan);
void sched_wake_tid(int tid);
void sched_tick(void);
void sched_preempt_disable(void);
void sched_preempt_enable(void);
void sched_set_base_prio(int tid, int prio);
int  sched_eff_prio_tid(int tid);
int  sched_current_id(void);
int  sched_started(void);   /* 0 before sched_init: single-threaded boot */
const char *sched_current_name(void);
int  sched_count(void);
const thread_t *sched_get(int idx);

void kmutex_init(kmutex_t *m);
void kmutex_lock(kmutex_t *m);
void kmutex_unlock(kmutex_t *m);

void sched_set_tss_updater(void (*fn)(uint64_t rsp0));
#endif
