#include "memory.h"
#include "kstdio.h"
#include "kstring.h"

#define HEAP_SIZE   (8u * 1024u * 1024u)
#define HDR         32u
#define ALIGN       16u
#define MIN_PAYLOAD 16u

#define M_ALLOC 0x4B4D414C4F43ULL
#define M_FREE  0x4B4D46524545ULL
#define M_SENT  0x4B4D53454E54ULL
#define F_FREE  1ULL

#define POISON_ALLOC 0xAA
#define POISON_FREE  0xDD
#define CANARY       0xC7

#define SMALL_MAX  512u
#define SMALL_BINS (SMALL_MAX / ALIGN)
#define LARGE_BINS 8u
#define NBINS      (SMALL_BINS + LARGE_BINS)

typedef struct block {
    uint64_t size_flags;
    uint64_t magic;
    uint64_t req_size;
    struct block *prev;
} block_t;

typedef char hdr_size_check[(sizeof(block_t) == HDR) ? 1 : -1];

typedef struct fl { struct block *next, *prev; } fl_t;

__attribute__((aligned(ALIGN)))
static uint8_t heap[HEAP_SIZE];

static block_t *first_blk, *sentinel;
static block_t *bins[NBINS];
static kheap_report_fn reporter;
static kheap_stats_t st;
static size_t capacity;
static int ready;

/* До появления IDT прерываний нет. После IDT заменить на pushfq/cli и popfq. */
static inline uint64_t heap_lock(void) {
    uint64_t f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void heap_unlock(uint64_t f) {
    __asm__ volatile("pushq %0; popfq" :: "r"(f) : "memory", "flags");
}

static inline size_t bsize(const block_t *b) { return (size_t)(b->size_flags & ~0xFULL); }
static inline int is_free(const block_t *b) { return (b->size_flags & F_FREE) != 0; }
static inline uint8_t *payload(block_t *b) { return (uint8_t *)b + HDR; }
static inline block_t *bnext(block_t *b) { return (block_t *)(payload(b) + bsize(b)); }
static inline block_t *bhdr(void *p) { return (block_t *)((uint8_t *)p - HDR); }
static inline fl_t *fl(block_t *b) { return (fl_t *)payload(b); }
static inline size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

/* --- вывод --- */
void kheap_report(const char *s) { if (reporter) reporter(s); }
void kheap_set_reporter(kheap_report_fn fn) { reporter = fn; }

static char *ap(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *au(char *p, uint64_t v) {
    char t[24]; int i = 0;
    if (!v) { *p++ = '0'; return p; }
    while (v) { t[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i) *p++ = t[--i];
    return p;
}
static char *ax(char *p, uint64_t v) {
    static const char *d = "0123456789ABCDEF";
    *p++ = '0'; *p++ = 'x';
    for (int i = 60; i >= 0; i -= 4) *p++ = d[(v >> i) & 0xF];
    return p;
}
static void rep_u(const char *a, uint64_t v, const char *b) {
    char buf[192]; char *p = buf;
    p = ap(p, a); p = au(p, v); p = ap(p, b); *p++ = '\n'; *p = 0;
    kheap_report(buf);
}
static void rep_x(const char *a, uint64_t v, const char *b) {
    char buf[192]; char *p = buf;
    p = ap(p, a); p = ax(p, v); p = ap(p, b); *p++ = '\n'; *p = 0;
    kheap_report(buf);
}
void kheap_dump_around(void *ptr) {
    uintptr_t h = (uintptr_t)heap;
    block_t *b = (block_t *)((uint8_t *)ptr - HDR);
    rep_x("cur       = ", (uint64_t)(uintptr_t)b, "");
    rep_x("cur.magic = ", b->magic, "");
    rep_u("cur.sf    = ", b->size_flags, "");
    if (b->prev && (uintptr_t)b->prev >= h && (uintptr_t)b->prev < h + HEAP_SIZE) {
        rep_x("prev      = ", (uint64_t)(uintptr_t)b->prev, "");
        rep_x("prev.magic= ", b->prev->magic, "");
        rep_u("prev.sf   = ", b->prev->size_flags, "");
    }
    block_t *n = (block_t *)((uint8_t *)b + HDR + (b->size_flags & ~0xFULL));
    if ((uintptr_t)n >= h && (uintptr_t)n < h + HEAP_SIZE) {
        rep_x("next      = ", (uint64_t)(uintptr_t)n, "");
        rep_x("next.magic= ", n->magic, "");
        rep_u("next.sf   = ", n->size_flags, "");
    }
}

static void fatal(const char *why, void *ptr, block_t *b) {
    st.corrupt_events++;
    kheap_report("\n*** HEAP CORRUPTION ***\n");
    kheap_report(why);
    kheap_report("\n");
    rep_x("ptr   = ", (uint64_t)(uintptr_t)ptr, "");
    if (b) {
        rep_x("block = ", (uint64_t)(uintptr_t)b, "");
        rep_x("magic = ", b->magic, "");
        rep_u("size  = ", bsize(b), "");
        rep_u("req   = ", b->req_size, "");
    }
    kheap_dump_around(ptr);
    kheap_report("SYSTEM HALTED\n");
    for (;;) __asm__ volatile ("hlt");
}

/* --- бины свободных блоков --- */
static int bin_of(size_t s) {
    if (s <= SMALL_MAX) return (int)(s / ALIGN) - 1;
    size_t kb = (s + 1023) >> 10;
    unsigned i = 0;
    while (((size_t)1 << i) < kb && i < LARGE_BINS - 1) i++;
    return (int)SMALL_BINS + (int)i;
}
static void bin_add(block_t *b) {
    int i = bin_of(bsize(b));
    fl_t *f = fl(b);
    f->prev = NULL;
    f->next = bins[i];
    if (bins[i]) fl(bins[i])->prev = b;
    bins[i] = b;
    st.free_blocks++;
    st.free_bytes += bsize(b);
    if (bsize(b) > st.largest_free) st.largest_free = bsize(b);
}
static void bin_del(block_t *b) {
    int i = bin_of(bsize(b));
    fl_t *f = fl(b);
    if (f->prev) fl(f->prev)->next = f->next;
    else bins[i] = f->next;
    if (f->next) fl(f->next)->prev = f->prev;
    f->next = f->prev = NULL;
    st.free_blocks--;
    st.free_bytes -= bsize(b);
}


void kinit_heap(void) {
    capacity = (HEAP_SIZE - 2 * HDR) & ~(size_t)(ALIGN - 1);
    first_blk = (block_t *)heap;
    first_blk->size_flags = capacity | F_FREE;
    first_blk->magic = M_FREE;
    first_blk->req_size = 0;
    first_blk->prev = NULL;

    sentinel = (block_t *)(heap + HDR + capacity);
    sentinel->size_flags = 0;
    sentinel->magic = M_SENT;
    sentinel->req_size = 0;
    sentinel->prev = first_blk;

    for (unsigned i = 0; i < NBINS; i++) bins[i] = NULL;
    memset(&st, 0, sizeof st);
    st.total_bytes = HEAP_SIZE;
    bin_add(first_blk);
    ready = 1;

}

static block_t *find_free(size_t size) {
    int i = bin_of(size);
    for (; i < (int)NBINS; i++) {
        block_t *b = bins[i];
        if (i < (int)SMALL_BINS) {
            if (b) return b;          /* точное попадание по размеру */
            continue;
        }
        for (; b; b = fl(b)->next)
            if (bsize(b) >= size) return b;
    }
    return NULL;
}

static void split_if_worth(block_t *b, size_t size) {
    size_t cur = bsize(b);
    if (cur < size + HDR + MIN_PAYLOAD) return;
    block_t *after = bnext(b);
    block_t *nb = (block_t *)(payload(b) + size);
    size_t rs = cur - size - HDR;
    nb->size_flags = rs | F_FREE;
    nb->magic = M_FREE;
    nb->req_size = 0;
    nb->prev = b;
    b->size_flags = size | (b->size_flags & 0xFULL);
    after->prev = nb;
    bin_add(nb);
}

void *kmalloc(size_t size) {
    if (!ready || size == 0) return NULL;
    uint64_t f = heap_lock();
    size_t need = align_up(size, ALIGN);
    if (need < MIN_PAYLOAD) need = MIN_PAYLOAD;
    if (need < size) { heap_unlock(f); return NULL; }   /* переполнение */

    block_t *b = find_free(need);
    if (!b) { st.failed_calls++; heap_unlock(f); return NULL; }

    bin_del(b);
    split_if_worth(b, need);
    /* Если сплит не состоялся, размер остаётся cur (>= need).
       Ужимать его нельзя: осиротеет хвост и сломается физическая цепочка. */
    size_t got = bsize(b);
    b->size_flags = got;            /* сбрасывает F_FREE, размер не теряется */
    b->magic = M_ALLOC;
    b->req_size = size;

    memset(payload(b), POISON_ALLOC, size);
    memset(payload(b) + size, CANARY, got - size);

    st.used_bytes += got;
    st.live_blocks++;
    st.alloc_calls++;
    if (st.used_bytes > st.peak_used) st.peak_used = st.used_bytes;
    heap_unlock(f);
    return payload(b);
}

static block_t *validate(void *ptr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t h = (uintptr_t)heap;
    if (p < h + HDR || p >= h + HEAP_SIZE) fatal("pointer outside heap", ptr, NULL);
    if ((p - h) & (ALIGN - 1)) fatal("misaligned pointer", ptr, NULL);
    block_t *b = bhdr(ptr);
    if (b->magic == M_FREE) fatal("double free", ptr, b);
    if (b->magic != M_ALLOC) fatal("bad magic: not a live heap block", ptr, b);
    if (is_free(b)) fatal("flags say FREE but magic says ALLOC", ptr, b);
    size_t s = bsize(b);
    if (s < MIN_PAYLOAD || (s & (ALIGN - 1)) ||
        (uintptr_t)payload(b) + s > h + HEAP_SIZE) fatal("bad block size", ptr, b);
    if (b->req_size > s) fatal("req_size > size", ptr, b);
    const uint8_t *c = payload(b) + b->req_size;
    for (size_t i = 0; i < s - b->req_size; i++)
        if (c[i] != CANARY) fatal("canary broken: buffer overflow", ptr, b);
    return b;
}

void kfree(void *ptr) {
    if (!ptr) return;
    uint64_t f = heap_lock();
    block_t *b = validate(ptr);
    size_t s = bsize(b);

    st.used_bytes -= s;
    st.live_blocks--;
    st.free_calls++;

    b->magic = M_FREE;
    b->size_flags = s | F_FREE;
    b->req_size = 0;
    memset(payload(b), POISON_FREE, s);

    /* слияние вперёд */
    block_t *n = bnext(b);
    while (n->magic == M_FREE) {
        size_t ns = bsize(n);
        bin_del(n);
        b->size_flags = (s + HDR + ns) | F_FREE;
        s = bsize(b);
        bnext(b)->prev = b;
        n = bnext(b);
    }
    /* слияние назад */
    block_t *p = b->prev;
    if (p && p->magic == M_FREE) {
        size_t ps = bsize(p);
        bin_del(p);
        p->size_flags = (ps + HDR + s) | F_FREE;
        p->req_size = 0;
        bnext(p)->prev = p;
        b = p;
    }
    bin_add(b);
    heap_unlock(f);
}

void *kcalloc(size_t n, size_t size) {
    if (n && size > (size_t)-1 / n) return NULL;
    size_t total = n * size;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }
    uint64_t f = heap_lock();
    block_t *b = validate(ptr);
    size_t old_req = (size_t)b->req_size;
    size_t s = bsize(b);
    size_t need = align_up(new_size, ALIGN);
    if (need < MIN_PAYLOAD) need = MIN_PAYLOAD;

    if (need <= s) {
        if (new_size > old_req) memset(payload(b) + old_req, POISON_ALLOC, new_size - old_req);
        memset(payload(b) + new_size, CANARY, s - new_size);
        b->req_size = new_size;
        heap_unlock(f);
        return ptr;
    }
    block_t *n = bnext(b);
    if (n->magic == M_FREE && s + HDR + bsize(n) >= need) {
        size_t ns = bsize(n);
        bin_del(n);
        size_t total = s + HDR + ns;
        b->size_flags = total;
        bnext(b)->prev = b;
        memset(payload(b) + old_req, POISON_ALLOC, new_size - old_req);
        memset(payload(b) + new_size, CANARY, total - new_size);
        b->req_size = new_size;
        st.used_bytes += total - s;
        heap_unlock(f);
        return ptr;
    }
    heap_unlock(f);
    void *np = kmalloc(new_size);
    if (!np) return NULL;
    memcpy(np, ptr, old_req < new_size ? old_req : new_size);
    kfree(ptr);
    return np;
}

void *kmalloc_aligned(size_t alignment, size_t size) {
    if (!size) return NULL;
    if (alignment < ALIGN) alignment = ALIGN;
    if (alignment & (alignment - 1)) return NULL;

    size_t need = size + alignment + HDR + MIN_PAYLOAD;
    void *raw = kmalloc(need);
    if (!raw) return NULL;

    block_t *b = bhdr(raw);
    uintptr_t p = (uintptr_t)raw;
    uintptr_t a = (p + alignment - 1) & ~(uintptr_t)(alignment - 1);
    if (a == p) return raw;
    if (a - p < HDR + MIN_PAYLOAD) a += alignment;

    size_t front = (size_t)(a - HDR - p);
    size_t cur = bsize(b);
    block_t *after = bnext(b);
    block_t *nb = (block_t *)(a - HDR);
    size_t ns = cur - front - HDR;

    nb->size_flags = ns;
    nb->magic = M_ALLOC;
    nb->req_size = size;
    nb->prev = b;
    after->prev = nb;

    b->size_flags = front | F_FREE;
    b->magic = M_FREE;
    b->req_size = 0;
    memset(payload(b), POISON_FREE, front);
    bin_add(b);

    memset((void *)a, POISON_ALLOC, size);
    memset((uint8_t *)a + size, CANARY, ns - size);
    st.used_bytes += ns - cur;
    return (void *)a;
}

void *kstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    void *p = kmalloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

void *kmemdup(const void *src, size_t n) {
    void *p = kmalloc(n);
    if (p) memcpy(p, src, n);
    return p;
}

size_t kheap_usable_size(void *ptr) {
    if (!ptr) return 0;
    uint64_t f = heap_lock();
    block_t *b = validate(ptr);
    size_t r = (size_t)b->req_size;
    heap_unlock(f);
    return r;
}

int kheap_check(void) {
    if (!ready) return -1;
    block_t *b = first_blk, *prev = NULL;
    uint32_t live = 0, freec = 0;
    size_t used = 0, freeb = 0, largest = 0;
    while (b != sentinel) {
        uintptr_t p = (uintptr_t)b;
        if (p < (uintptr_t)heap || p + HDR > (uintptr_t)sentinel) {
            rep_x("check: block out of range ", p, ""); return -2;
        }
        if (b->prev != prev) { rep_x("check: prev link broken at ", p, ""); return -3; }
        size_t s = bsize(b);
        if (s < MIN_PAYLOAD || (s & (ALIGN - 1))) {
            rep_x("check: bad size at ", p, ""); return -4;
        }
        if (is_free(b)) {
            if (b->magic != M_FREE) { rep_x("check: free magic mismatch ", p, ""); return -5; }
            freec++; freeb += s;
            if (s > largest) largest = s;
        } else {
            if (b->magic != M_ALLOC) { rep_x("check: alloc magic mismatch ", p, ""); return -6; }
            if (b->req_size > s) { rep_x("check: req>size at ", p, ""); return -7; }
            const uint8_t *c = payload(b) + b->req_size;
            for (size_t i = 0; i < s - b->req_size; i++)
                if (c[i] != CANARY) { rep_x("check: canary broken at ", p, ""); return -8; }
            live++; used += s;
        }
        prev = b;
        b = bnext(b);
    }
    st.live_blocks = live;
    st.free_blocks = freec;
    st.used_bytes = used;
    st.free_bytes = freeb;
    st.largest_free = largest;
    return 0;
}

void kheap_get_stats(kheap_stats_t *out) {
    st.overhead_bytes = (uint64_t)(st.live_blocks + st.free_blocks + 1) * HDR;
    *out = st;
}

void kheap_dump(void) {
    kheap_stats_t s;
    kheap_check();
    kheap_get_stats(&s);
    kheap_report("\n--- KMALLOC STATS ---\n");
    rep_u("total    = ", s.total_bytes, " bytes");
    rep_u("used     = ", s.used_bytes, " bytes");
    rep_u("free     = ", s.free_bytes, " bytes");
    rep_u("overhead = ", s.overhead_bytes, " bytes");
    rep_u("largest  = ", s.largest_free, " bytes");
    rep_u("peak     = ", s.peak_used, " bytes");
    rep_u("live blk = ", s.live_blocks, "");
    rep_u("free blk = ", s.free_blocks, "");
    rep_u("allocs   = ", s.alloc_calls, "");
    rep_u("frees    = ", s.free_calls, "");
    rep_u("failed   = ", s.failed_calls, "");
    rep_u("corrupt  = ", s.corrupt_events, "");
}
