/* MyOS — ARM7TDMI startup */
        .arm
        .section .vectors, "ax"
        .align 4

        b   _start              /* 0x00 Reset       */
        b   _hang               /* 0x04 Undefined   */
        b   switch_task         /* 0x08 SWI         */
        b   _hang               /* 0x0C Prefetch    */
        b   _hang               /* 0x10 Data abort  */
        b   _hang               /* 0x14 Reserved    */
        b   irq_handler         /* 0x18 IRQ         */
        b   _hang               /* 0x1C FIQ         */

_hang:
        b   _hang

        .section .text
        .align 4
        .global _start
_start:
        ldr     r0, =_stack_top

        msr     CPSR_c, #0xD2       /* IRQ mode */
        mov     sp, r0
        sub     r0, r0, #0x1000

        msr     CPSR_c, #0xD3       /* SVC mode */
        mov     sp, r0
        sub     r0, r0, #0x4000

        msr     CPSR_c, #0xDF       /* SYS mode */
        mov     sp, r0
        sub     r0, r0, #0x4000

        msr     CPSR_c, #0xD3       /* Back to SVC */

        /* Clear bss */
        ldr     r1, =__bss_start
        ldr     r2, =__bss_end
        mov     r3, #0
1:      cmp     r1, r2
        strlo   r3, [r1], #4
        blo     1b

        bl      kernel_main
_hang2:
        b       _hang2

/* IRQ — тик таймера, только инкремент tick_count */
        .global irq_handler
irq_handler:
        stmdb   sp!, {r0-r3, r12, lr}
        bl      irq_dispatch
        ldmia   sp!, {r0-r3, r12, lr}
        subs    pc, lr, #4
