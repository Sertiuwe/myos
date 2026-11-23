#include <stdint.h>
#include "kstdio.h"
#include "memory.h"
#include "syscall.h"
#include "sched.h"

/* =========================================================================
   RING 3. Built on measured facts, not assumptions:
     stage2 GDT = 5 entries: [3]=0x18 kcode64 (L=1), [4]=0x20 kdata64
     CS=0x18 DS=SS=ES=0x20, TR=0, paging on, U/S cleared everywhere.
   We COPY those 5 entries verbatim and append user segments + TSS, so no
   existing selector changes meaning. Then we set U/S so Ring 3 can run.
   ========================================================================= */

#define SEL_KCODE 0x18
#define SEL_KDATA 0x20
#define SEL_UCODE 0x2B   /* index 5 | RPL 3 */
#define SEL_UDATA 0x33   /* index 6 | RPL 3 */
#define SEL_TSS   0x38   /* index 7 (16-byte descriptor: 7 and 8) */

#define PA(e) ((e) & 0x000FFFFFFFFFF000ULL)

struct desc_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static uint64_t         gdt_new[9] __attribute__((aligned(8)));
static struct desc_ptr  gp_new;
static struct tss_entry tss __attribute__((aligned(16)));
static uint8_t          tss_stack[16384] __attribute__((aligned(16)));
static int              ring3_ready;
static char             u_done_chan;
static volatile int     u_done;
static volatile int     u_exit_code;

static void tss_set_rsp0(uint64_t top) {
    tss.rsp0 = top ? top : (uint64_t)(uintptr_t)&tss_stack[sizeof(tss_stack)];
}

extern void syscall_entry(void);

int64_t syscall_handler_c(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6) {
    return sys_dispatch((uint32_t)num, a1, a2, a3, a4, a5, a6);
}

/* ---------------- diagnostics (read-only) ---------------- */
void kernel_ring3_diag(void) {
    struct desc_ptr gdtr, idtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    __asm__ volatile("sidt %0" : "=m"(idtr));
    uint16_t cs = 0, ds = 0, ss = 0, tr = 0;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile("mov %%ds, %0" : "=r"(ds));
    __asm__ volatile("mov %%ss, %0" : "=r"(ss));
    __asm__ volatile("str %0" : "=m"(tr));
    kprintf("--- RING3 DIAG ---\n");
    kprintf("GDTR base=%p limit=%u entries=%u\n",
            (void *)(uintptr_t)gdtr.base, (unsigned)gdtr.limit,
            (unsigned)((gdtr.limit + 1) / 8));
    kprintf("CS=%x DS=%x SS=%x TR=%x  ready=%d\n",
            (unsigned)cs, (unsigned)ds, (unsigned)ss, (unsigned)tr, ring3_ready);
    const uint64_t *g = (const uint64_t *)(uintptr_t)gdtr.base;
    int n = (gdtr.limit + 1) / 8;
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) {
        unsigned acc = (unsigned)((g[i] >> 40) & 0xFF);
        unsigned fl  = (unsigned)((g[i] >> 52) & 0x0F);
        kprintf("  [%d] sel=%02x raw=%p acc=%02x DPL=%u L=%u\n",
                i, (unsigned)(i * 8), (void *)(uintptr_t)g[i], acc,
                (acc >> 5) & 3u, (fl >> 1) & 1u);
    }
    uint64_t cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t va = (uint64_t)(uintptr_t)&kernel_ring3_diag;
    const uint64_t *pml4 = (const uint64_t *)(uintptr_t)PA(cr3);
    uint64_t e4 = pml4[(va >> 39) & 0x1FF];
    const uint64_t *pdpt = (const uint64_t *)(uintptr_t)PA(e4);
    uint64_t e3 = pdpt[(va >> 30) & 0x1FF];
    const uint64_t *pd = (const uint64_t *)(uintptr_t)PA(e3);
    uint64_t e2 = pd[(va >> 21) & 0x1FF];
    kprintf("kernel page U/S chain: %u/%u/%u => USER %s\n",
            (unsigned)((e4 >> 2) & 1), (unsigned)((e3 >> 2) & 1), (unsigned)((e2 >> 2) & 1),
            ((e4 & 4) && (e3 & 4) && (e2 & 4)) ? "ALLOWED" : "DENIED");
    kprintf("--- DIAG DONE ---\n");
}

/* ---------------- paging: allow user access ---------------- */
static void paging_user_allow(void) {
    uint64_t cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)PA(cr3);
    for (int i = 0; i < 512; i++) {
        if (!(pml4[i] & 1)) continue;
        pml4[i] |= 4;
        uint64_t *pdpt = (uint64_t *)(uintptr_t)PA(pml4[i]);
        for (int j = 0; j < 512; j++) {
            if (!(pdpt[j] & 1)) continue;
            pdpt[j] |= 4;
            if (pdpt[j] & 0x80) continue;
            uint64_t *pd = (uint64_t *)(uintptr_t)PA(pdpt[j]);
            for (int k = 0; k < 512; k++) {
                if (!(pd[k] & 1)) continue;
                pd[k] |= 4;
                if (pd[k] & 0x80) continue;
                uint64_t *pt = (uint64_t *)(uintptr_t)PA(pd[k]);
                for (int l = 0; l < 512; l++)
                    if (pt[l] & 1) pt[l] |= 4;
            }
        }
    }
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

/* ---------------- init ---------------- */
/* Install the int 0x80 gate. Independent of GDT/TSS/paging: syslib needs it
   from the very first syscall, long before any Ring 3 transition. */
void kernel_syscall_init(void) {
    struct desc_ptr idtp;
    __asm__ volatile("sidt %0" : "=m"(idtp));
    uint8_t *gate = (uint8_t *)(uintptr_t)idtp.base + 0x80 * 16;
    uint64_t addr = (uint64_t)(uintptr_t)syscall_entry;
    uint16_t lo = (uint16_t)(addr & 0xFFFF);
    uint16_t mid = (uint16_t)((addr >> 16) & 0xFFFF);
    uint32_t hi = (uint32_t)(addr >> 32);
    uint16_t sel = SEL_KCODE;
    uint8_t  ist = 0, attr = 0xEE;   /* present, DPL 3, 64-bit interrupt gate */
    uint32_t zero = 0;
    __builtin_memcpy(gate + 0, &lo, 2);
    __builtin_memcpy(gate + 2, &sel, 2);
    gate[4] = ist;
    gate[5] = attr;
    __builtin_memcpy(gate + 6, &mid, 2);
    __builtin_memcpy(gate + 8, &hi, 4);
    __builtin_memcpy(gate + 12, &zero, 4);
    kprintf("SYSCALL: int 0x80 gate installed (DPL 3)\n");
}

void kernel_init_ring3(void) {
    struct desc_ptr old;
    __asm__ volatile("sgdt %0" : "=m"(old));
    int n = (old.limit + 1) / 8;
    if (n != 5) {
        kprintf("RING3: unexpected GDT size %d, aborting\n", n);
        return;
    }
    const uint64_t *src = (const uint64_t *)(uintptr_t)old.base;
    for (int i = 0; i < 5; i++) gdt_new[i] = src[i];   /* verbatim copy */

    gdt_new[5] = 0x0020FA0000000000ULL;  /* user code64: L=1, DPL=3 */
    gdt_new[6] = 0x0000F30000000000ULL;  /* user data64: DPL=3, RW  */

    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t lim  = sizeof(struct tss_entry) - 1;
    gdt_new[7] = (lim & 0xFFFF)
               | ((base & 0xFFFF) << 16)
               | (((base >> 16) & 0xFF) << 32)
               | (0x89ULL << 40)
               | (((lim >> 16) & 0x0F) << 48)
               | (((base >> 24) & 0xFF) << 56);
    gdt_new[8] = (base >> 32) & 0xFFFFFFFFULL;

    for (unsigned i = 0; i < sizeof(tss); i++) ((uint8_t *)&tss)[i] = 0;
    tss.rsp0 = (uint64_t)(uintptr_t)&tss_stack[sizeof(tss_stack)];
    tss.iomap_base = sizeof(struct tss_entry);

    gp_new.limit = sizeof(gdt_new) - 1;
    gp_new.base  = (uint64_t)(uintptr_t)gdt_new;

    __asm__ volatile("lgdt %0" : : "m"(gp_new) : "memory");
    /* entries 0..4 are byte-identical: CS/DS/SS keep working, no reload needed */
    __asm__ volatile("ltr %0" : : "r"((uint16_t)SEL_TSS));
    kprintf("RING3: GDT extended to 9 entries, TSS at sel 0x38\n");

    paging_user_allow();
    kprintf("RING3: page U/S bits set, TLB flushed\n");

    kernel_syscall_init();

    sched_set_tss_updater(tss_set_rsp0);
    ring3_ready = 1;
    kprintf("RING3: int 0x80 gate installed (DPL 3). Ready.\n");
}

/* ---------------- user-side syscall (no kernel calls, no port I/O) -------- */
static int64_t u_call(uint64_t num, uint64_t a1, uint64_t a2) {
    int64_t r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(num), "S"(a1), "d"(a2)
                     : "memory");
    return r;
}

static void u_puts(const char *s) { u_call(SYS_KPRINT, (uint64_t)(uintptr_t)s, 0); }

static void u_num(const char *prefix, int64_t v, const char *suffix) {
    char buf[96];
    int n = 0;
    for (const char *p = prefix; *p && n < 80; p++) buf[n++] = *p;
    if (v < 0) { buf[n++] = '-'; v = -v; }
    char tmp[24];
    int t = 0;
    do { tmp[t++] = (char)('0' + (v % 10)); v /= 10; } while (v && t < 20);
    while (t > 0 && n < 80) buf[n++] = tmp[--t];
    for (const char *p = suffix; *p && n < 90; p++) buf[n++] = *p;
    buf[n] = '\0';
    u_puts(buf);
}

/* This code executes at CPL=3. Only int 0x80 crosses into the kernel. */
void user_task_test(void) {
    u_puts("\n>>> RING3: hello from user mode <<<\n");
    u_num(">>> RING3: SYS_TID returned ", u_call(SYS_TID, 0, 0), "\n");
    u_num(">>> RING3: SYS_UPTIME_MS returned ", u_call(SYS_UPTIME_MS, 0, 0), " ms\n");
    u_puts(">>> RING3: exiting through SYS_EXIT\n");
    u_call(SYS_EXIT, 0, 0);
    for (;;) { }
}

/* ---------------- the transition ---------------- */
void kernel_switch_to_user_mode(void) {
    if (!ring3_ready) {
        kprintf("RING3: not initialized, run testring3 first\n");
        return;
    }
    uint8_t *ustack = kmalloc(16384);
    if (!ustack) { kprintf("RING3: user stack alloc failed\n"); return; }
    uint64_t user_rsp = ((uint64_t)(uintptr_t)ustack + 16384) & ~0xFULL;
    uint64_t entry = (uint64_t)(uintptr_t)&user_task_test;

    kprintf("RING3: iretq -> CS=0x2B SS=0x33 RIP=%p RSP=%p\n",
            (void *)(uintptr_t)entry, (void *)(uintptr_t)user_rsp);

    __asm__ volatile(
        "cli\n"
        "mov %0, %%rsp\n"
        "pushq %1\n"        /* SS  = user data | RPL3 */
        "pushq %0\n"        /* RSP = user stack */
        "pushq $0x202\n"    /* RFLAGS: IF=1 */
        "pushq %2\n"        /* CS  = user code | RPL3 */
        "pushq %3\n"        /* RIP */
        "iretq\n"
        :
        : "r"(user_rsp), "r"((uint64_t)SEL_UDATA), "r"((uint64_t)SEL_UCODE), "r"(entry)
        : "memory");

    kprintf("RING3: iretq returned - should not happen\n");
}

/* ---------------- process lifecycle ---------------- */

void kernel_user_exit(int code) {
    kprintf("EXIT: reached, code=%d, tid=%d\n", code, sched_current_id());
    u_exit_code = code;
    u_done = 1;
    sched_wake(&u_done_chan);
    kprintf("EXIT: woke waiter, calling sched_exit\n");
    sched_exit();
    kprintf("EXIT: sched_exit RETURNED (bug)\n");
}

struct u_start { uint64_t entry, stack; };

static void user_thread(void *arg) {
    struct u_start *u = (struct u_start *)arg;
    uint64_t entry = u->entry, stack = u->stack;
    __asm__ volatile(
        "cli\n"
        "mov %0, %%rsp\n"
        "pushq %1\n"
        "pushq %0\n"
        "pushq $0x202\n"
        "pushq %2\n"
        "pushq %3\n"
        "iretq\n"
        :
        : "r"(stack), "r"((uint64_t)SEL_UDATA), "r"((uint64_t)SEL_UCODE), "r"(entry)
        : "memory");
    for (;;) { }
}

int kernel_user_spawn(uint64_t entry, uint64_t stack_top, const char *name) {
    static struct u_start us;
    us.entry = entry;
    us.stack = stack_top;
    u_done = 0;
    u_exit_code = 0;
    int tid = sched_spawn(name ? name : "user", user_thread, &us, 32768);
    return tid;
}

int kernel_user_wait(void) {
    while (!u_done) sched_block_on(&u_done_chan);
    return u_exit_code;
}

void kernel_user_demo(void) {
    if (!ring3_ready) kernel_init_ring3();
    if (!ring3_ready) return;
    uint8_t *st = kmalloc(16384);
    if (!st) { kprintf("RING3: stack alloc failed\n"); return; }
    uint64_t top = ((uint64_t)(uintptr_t)st + 16384) & ~0xFULL;
    if (kernel_user_spawn((uint64_t)(uintptr_t)&user_task_test, top, "usertest") < 0) {
        kprintf("RING3: spawn failed\n");
        return;
    }
    kprintf("RING3: parent waiting...\n");
    int rc = kernel_user_wait();
    kprintf("RING3: process exited with code %d, back to shell\n", rc);
}
