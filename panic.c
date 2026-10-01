/* ============================================================
 * panic.c — kernel panic с дампом регистров
 *   * Использует raw UART напрямую — работает даже если
 *     print.c / UART IRQ сломаны
 * ============================================================ */
#include "panic.h"

#define UART0_BASE 0x10009000u
#define UART_DR (*(volatile uint32_t*)(UART0_BASE + 0x00))
#define UART_FR (*(volatile uint32_t*)(UART0_BASE + 0x18))

/* Raw UART без зависимостей */
static void raw_putc(char c) {
    for (int i = 0; i < 100000; i++) {
        if (!(UART_FR & (1u << 5))) break;
    }
    UART_DR = (uint32_t)(uint8_t)c;
}

static void raw_puts(const char *s) {
    while (*s) {
        if (*s == '\n') raw_putc('\r');
        raw_putc(*s++);
    }
}

static void raw_puthex(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    raw_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        raw_putc(hex[(v >> i) & 0xF]);
    }
}

static void raw_pad(int n) {
    while (n-- > 0) raw_putc(' ');
}

static void panic_banner(void) {
    raw_puts("\n\n");
    raw_puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    raw_puts("             KERNEL PANIC              \n");
    raw_puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
}

void panic(const char *msg) {
    /* Запрещаем IRQ */
    __asm__ volatile ("cpsid if" ::: "memory");
    panic_banner();
    raw_puts("Reason: ");
    raw_puts(msg ? msg : "(none)");
    raw_puts("\n\n");
    raw_puts("System halted. Reset to restart.\n");
    while (1) { __asm__ volatile (""); }
}

void panic_with_regs(const char *msg, const uint32_t *regs) {
    __asm__ volatile ("cpsid if" ::: "memory");
    panic_banner();
    raw_puts("Reason: ");
    raw_puts(msg ? msg : "(none)");
    raw_puts("\n\n");
    raw_puts("--- CPU registers ---\n");
    for (int i = 0; i < 16; i++) {
        raw_puts("  R");
        if (i < 10) raw_putc('0' + i);
        else { raw_putc('1'); raw_putc('0' + (i - 10)); }
        raw_puts(" = ");
        raw_puthex(regs[i]);
        if (i == 13) raw_puts("  (SP)");
        if (i == 14) raw_puts("  (LR)");
        if (i == 15) raw_puts("  (PC)");
        raw_puts("\n");
    }
    raw_puts("\nSystem halted.\n");
    while (1) { __asm__ volatile (""); }
}

void panic_regs(const char *msg) {
    uint32_t regs[16];
    __asm__ volatile (
        "str r0,  [%0, #0]\n"
        "str r1,  [%0, #4]\n"
        "str r2,  [%0, #8]\n"
        "str r3,  [%0, #12]\n"
        "str r4,  [%0, #16]\n"
        "str r5,  [%0, #20]\n"
        "str r6,  [%0, #24]\n"
        "str r7,  [%0, #28]\n"
        "str r8,  [%0, #32]\n"
        "str r9,  [%0, #36]\n"
        "str r10, [%0, #40]\n"
        "str r11, [%0, #44]\n"
        "str r12, [%0, #48]\n"
        "str sp,  [%0, #52]\n"
        "str lr,  [%0, #56]\n"
        "str pc,  [%0, #60]\n"
        :: "r"(regs)
    );
    panic_with_regs(msg, regs);
}
