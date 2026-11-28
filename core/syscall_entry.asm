[bits 64]
global syscall_entry
extern syscall_handler_c

; int 0x80 from Ring 3. CPU already switched to TSS.RSP0 and pushed
; SS/RSP/RFLAGS/CS/RIP. We save GPRs, call the dispatcher, put the return
; value into the saved RAX slot, restore, and iretq back to user mode.
syscall_entry:
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

    mov rdi, rax        ; num
    mov rax, r10        ; a6
    push rax
    call syscall_handler_c
    add rsp, 8

    mov [rsp + 112], rax   ; overwrite saved RAX with the return value

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
    iretq
