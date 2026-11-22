[bits 64]

extern exception_handler
extern irq_handler

common_exception:
    mov rdi, [rsp + 120]
    mov rsi, [rsp + 128]
    mov rdx, [rsp + 136]
    mov rcx, [rsp + 160]
    mov r8,  [rsp + 48]
    mov r9, 0
    cmp rdi, 14
    jne .no_cr2
    mov rax, cr2
    mov r9, rax
.no_cr2:
    call exception_handler
    cli
    hlt
    jmp $

%macro POP_REGS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

common_irq:
    mov rdi, [rsp + 120]
    call irq_handler
    POP_REGS
    add rsp, 16             ; skip error_code + vector
    iretq

%macro PUSH_REGS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push qword 0
    push qword %1
    PUSH_REGS
    jmp common_exception
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push qword %1
    PUSH_REGS
    jmp common_exception
%endmacro

%macro IRQ_STUB 1
global isr_stub_%1
isr_stub_%1:
    push qword 0
    push qword %1
    PUSH_REGS
    jmp common_irq
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

IRQ_STUB 32
IRQ_STUB 33

global isr_stub_default
isr_stub_default:
    push qword 0
    push qword 0xFF
    PUSH_REGS
    jmp common_exception
