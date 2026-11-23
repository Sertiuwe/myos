#ifndef MYOS_APP_H
#define MYOS_APP_H
#include <stdint.h>

/* ==== Application format for MyOS ====
   .myos.meta     : identity + resource requirements (validated by loader)
   .myos.imports  : name -> syscall number, patched by the loader at load time
   Everything else is a normal static ELF64 executable linked at 0x2000000. */

typedef struct {
    char     magic[8];      /* "MYOSAPP1" */
    uint32_t version;       /* 1 */
    uint32_t flags;
    uint32_t stack_size;
    uint32_t heap_size;
    char     name[32];
} myos_meta_t;

typedef struct {
    char     name[32];
    uint32_t sysnum;        /* 0 in the file, filled by the loader */
    uint32_t flags;
} myos_import_t;

#define MYOS_APP_META(nm, stk) \
    __attribute__((section(".myos.meta"), used)) \
    const myos_meta_t myos_meta = { {'M','Y','O','S','A','P','P','1'}, 1, 0, (stk), 0, nm }

#define MYOS_IMPORTS(...) \
    __attribute__((section(".myos.imports"), used)) \
    myos_import_t myos_imports[] = { __VA_ARGS__ }

static inline int64_t app_call(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(num), "S"(a1), "d"(a2), "c"(a3)
                     : "memory");
    return r;
}

#endif
