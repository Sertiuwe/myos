#ifndef SELFTEST_H
#define SELFTEST_H

/* Запускает self-test всех подсистем.
 * Возвращает количество провалов.
 * 0 = всё ок, >0 = есть проблемы. */
int selftest_run(void);

#endif
