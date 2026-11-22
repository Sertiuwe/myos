[bits 64]

extern exception_handler

common_handler:
    mov rdi, [rsp + 120]   ; vector number
    mov rsi, [rsp + 128]   ; error code
    mov rdx, [rsp + 136]   ; RIP
    mov rcx, [rsp + 160]   ; original RSP
    mov r8,  [rsp + 48]    ; RBP
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
    jmp common_handler
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push qword %1
    PUSH_REGS
    jmp common_handler
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

global isr_stub_default
isr_stub_default:
    push qword 0
    push qword 0xFF
    PUSH_REGS
    jmp common_handler
