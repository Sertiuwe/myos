#ifndef PANIC_H
#define PANIC_H

#include "types.h"

/* Halts the system with a message. Использует raw UART. */
void panic(const char *msg) __attribute__((noreturn));

/* То же, но с dump регистров */
void panic_with_regs(const char *msg, const uint32_t *regs) __attribute__((noreturn));

/* Автоматически снимает регистры и паникует */
void panic_regs(const char *msg) __attribute__((noreturn));

/* Универсальный assert */
#define ASSERT(cond, msg) do { \
    if (!(cond)) panic("ASSERT: " msg); \
} while (0)

#endif
