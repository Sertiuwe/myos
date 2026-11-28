#ifndef SXNL_H
#define SXNL_H
#include <stdint.h>

/* Sxnl Security X, Not Linux - security subsystem */

enum { SXNL_EV_FAULT = 1, SXNL_EV_BADPTR, SXNL_EV_KILL, SXNL_EV_WX };
enum { SXNL_ACT_CONTINUE = 0, SXNL_ACT_KILL = 1, SXNL_ACT_PANIC = 2 };

void sxnl_init(void);
/* validate a user-space buffer; 1 = ok */
int  sxnl_user_ok(const void *p, uint32_t len);
/* called on every exception; returns policy action */
int  sxnl_fault(uint64_t vec, uint64_t addr, int user_mode);
/* called when syscall receives an invalid user pointer */
void sxnl_badptr(uint64_t ptr);
/* audit */
void sxnl_dump(void);
void sxnl_selftest(void);
#endif
