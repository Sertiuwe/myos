/* MyOS — переключение контекста
 * switch_task():
 *   - сохраняет R0-R12, LR, SPSR текущей задачи на её стек
 *   - сохраняет SP в tasks[current].sp
 *   - вызывает schedule() чтобы выбрать следующую
 *   - восстанавливает SP следующей
 *   - восстанавливает R0-R12, LR, SPSR
 *   - movs pc, lr — возврат с восстановлением CPSR из SPSR
 */

        .extern tasks
        .extern current
        .extern task_t_size

        .global switch_task
switch_task:
        /* Сохраняем контекст: push R0-R12, LR на стек */
        stmdb   sp!, {r0-r12, lr}

        /* Сохраняем SPSR */
        mrs     r0, SPSR
        stmdb   sp!, {r0}

        /* current >= 0? */
        ldr     r0, =current
        ldr     r1, [r0]
        cmp     r1, #0
        blt     no_save

        /* r2 = &tasks[0] */
        ldr     r2, =tasks
        /* r3 = task_t_size */
        ldr     r3, =task_t_size
        ldr     r3, [r3]
        /* r4 = current * task_t_size */
        mul     r4, r1, r3
        add     r2, r2, r4
        /* tasks[current].sp = sp */
        str     sp, [r2]

no_save:
        /* Выбираем следующую задачу */
        bl      schedule

        /* sp = tasks[current].sp */
        ldr     r0, =current
        ldr     r1, [r0]
        ldr     r2, =tasks
        ldr     r3, =task_t_size
        ldr     r3, [r3]
        mul     r4, r1, r3
        add     r2, r2, r4
        ldr     sp, [r2]

        /* Восстанавливаем SPSR */
        ldmia   sp!, {r0}
        msr     SPSR, r0

        /* Восстанавливаем R0-R12, LR */
        ldmia   sp!, {r0-r12, lr}
        /* movs pc, lr — возврат с восстановлением CPSR из SPSR */
        movs    pc, lr
