[bits 64]

; void ctx_switch(uint64_t *save_rsp, uint64_t load_rsp)
; Callee-saved only. Interrupts MUST be off on entry.
; The resumed side returns right after its own call to ctx_switch.
global ctx_switch
ctx_switch:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov  [rdi], rsp
    mov  rsp, rsi
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbp
    pop  rbx
    ret
