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

    mov dx, 0x3F8
    mov al, '1'
    out dx, al

    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; stage2: 32 сектора, LBA 1 -> 0x8000
    mov si, dap_stage2
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; kernel часть 1: 128 секторов, LBA 33 -> 0x20000
    mov si, dap_k1
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; kernel часть 2: 128 секторов, LBA 161 -> 0x30000
    ; (делим на два чтения по 64 КБ, чтобы не пересекать DMA-границу)
    mov si, dap_k2
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; kernel часть 3: 128 секторов, LBA 289 -> 0x40000
    mov si, dap_k3
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
    dw 32
    dw 0x0000
    dw 0x0800
    dq 1

align 4
dap_k1:
    db 16, 0
    dw 128
    dw 0x0000
    dw 0x2000
    dq 33

align 4
dap_k2:
    db 16, 0
    dw 128
    dw 0x0000
    dw 0x3000
    dq 161

dap_k3:
    db 16, 0
    dw 128
    dw 0x0000
    dw 0x4000
    dq 289

times 510 - ($ - $$) db 0
dw 0xAA55
