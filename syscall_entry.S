[bits 64]
global syscall_entry
extern syscall_handler_c

syscall_entry:
    ; 1. Save all general-purpose registers (SysV ABI caller-saved + callee-saved)
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; 2. Call the C handler
    ; SysV ABI: rdi=num, rsi=a1, rdx=a2, rcx=a3, r8=a4, r9=a5
    ; The syscall number was in rax, move it to rdi for the first argument
    mov rdi, rax
    call syscall_handler_c

    ; 3. Restore registers in exact reverse order
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    
    ; 4. Return to user space
    iretq
