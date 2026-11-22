#ifndef SCHED_H
#define SCHED_H
#include <stdint.h>

#define SCHED_MAX_THREADS   24
#define SCHED_SLICE_TICKS   20      /* 20 ms @ 1000 Hz */
#define SCHED_PRIO_LEVELS   4
#define PRIO_HI   0                 /* compositor, latency-critical */
#define PRIO_NORM 1                 /* app windows, shell */
#define PRIO_BG   2                 /* background workers */
#define PRIO_IDLE 3                 /* idle task */
#define MTX_NO_BOOST 255
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
void sched_wake(void *chan);
void sched_wake_tid(int tid);
void sched_tick(void);
void sched_preempt_disable(void);
void sched_preempt_enable(void);
void sched_set_base_prio(int tid, int prio);
int  sched_eff_prio_tid(int tid);
int  sched_current_id(void);
const char *sched_current_name(void);
int  sched_count(void);
const thread_t *sched_get(int idx);

void kmutex_init(kmutex_t *m);
void kmutex_lock(kmutex_t *m);
void kmutex_unlock(kmutex_t *m);

#endif
