[bits 16]
[org 0x7c00]

_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov [boot_drive], dl

    ; Маркер '1' в COM1
    mov dx, 0x3F8
    mov al, '1'
    out dx, al

    ; Проверка LBA (EDD)
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; stage2: 32 сектора, LBA 1 -> 0x0800:0x0000 (физ. 0x8000)
    mov si, dap_stage2
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; kernel: 64 сектора, LBA 33 -> 0x2000:0x0000 (физ. 0x20000)
    mov si, dap_kernel
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    jmp 0x0800:0x0000

disk_error:
    mov dx, 0x3F8
    mov al, 'E'
    out dx, al
    mov si, msg_disk
    call print16
    hlt
    jmp $

print16:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print16
.done:
    ret

msg_disk:   db "Disk read error!", 13, 10, 0
boot_drive: db 0

align 4
dap_stage2:
    db 16, 0
    dw 32          ; секторов
    dw 0x0000      ; offset
    dw 0x0800      ; segment -> 0x8000
    dq 1           ; LBA start

align 4
dap_kernel:
    db 16, 0
    dw 64
    dw 0x0000
    dw 0x2000      ; -> 0x20000
    dq 33

times 510 - ($ - $$) db 0
dw 0xAA55
