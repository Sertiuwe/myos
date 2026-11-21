[bits 16]
[org 0x0000]

%define SERIAL 0x3F8
%define KERNEL_SRC  0x20000
%define KERNEL_DST  0x100000
%define KERNEL_SIZE 131072
%define PML4_ADDR   0xA00000

_start:
    cld
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, 0x7c00
    xor ax, ax
    mov es, ax

    mov dx, SERIAL
    mov al, '2'
    out dx, al

    ; A20
    in al, 0x92
    or al, 2
    out 0x92, al

    ; E820 -> 0x9000
    mov di, 0x9000
    xor ebx, ebx
    xor bp, bp
.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc bp
    add di, 24
    test ebx, ebx
    jnz .e820_loop
.e820_done:
    mov [e820_count], bp

    ; VESA 1024x768x32
    mov ax, 0x4F02
    mov bx, 0x118
    int 0x10
    cmp ax, 0x004F
    jne vesa_fail
    mov ax, 0x4F01
    mov cx, 0x118
    mov di, 0x8800
    int 0x10
    cmp ax, 0x004F
    jne vesa_fail
    ; VBE-блок записан BIOS по ES:DI = 0x0000:0x8800.
    ; DS = 0x0800, поэтому читать нужно ТОЛЬКО через es:, иначе берём мусор.
    mov eax, [es:0x8800 + 40]      ; PhysBasePtr
    mov [fb_addr], eax
    mov ax, [es:0x8800 + 16]       ; BytesPerScanline
    mov [fb_pitch], ax
    mov ax, [es:0x8800 + 18]       ; XResolution
    mov [fb_width], ax
    mov ax, [es:0x8800 + 20]       ; YResolution
    mov [fb_height], ax
    mov al, [es:0x8800 + 25]       ; BitsPerPixel
    xor ah, ah
    mov [fb_bpp], ax

    mov dx, SERIAL
    mov al, '3'
    out dx, al

    ; База GDT = линейный адрес (0x8000 + offset)
    mov eax, gdt64
    add eax, 0x8000
    mov [gdt_base], eax
    cli
    lgdt [gdt_ptr]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; o32 far jump: 66 EA <offset32> <selector16>
    db 0x66, 0xEA
    dd pm_entry + 0x8000
    dw 0x08

vesa_fail:
    mov dx, SERIAL
    mov al, 'V'
    out dx, al
    hlt
    jmp $

[bits 32]
pm_entry:
    cld
    mov dx, SERIAL
    mov al, 'a'
    out dx, al

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x70000

    mov dx, SERIAL
    mov al, 'b'
    out dx, al

    ; kernel: 0x20000 -> 0x100000
    mov esi, KERNEL_SRC
    mov edi, KERNEL_DST
    mov ecx, KERNEL_SIZE / 4
    rep movsd

    mov dx, SERIAL
    mov al, 'c'
    out dx, al

    ; Таблицы страниц: PML4 + PDPT + 4 PD (identity 0..4GB)
    mov edi, PML4_ADDR
    xor eax, eax
    mov ecx, 6 * 1024
    rep stosd

    mov dword [PML4_ADDR], 0xA01003
    mov dword [PML4_ADDR + 4], 0
    mov ecx, 4
    mov ebx, 0xA02003
    mov edi, 0xA01000
.pdpt_loop:
    mov [edi], ebx
    mov dword [edi + 4], 0
    add ebx, 0x1000
    add edi, 8
    loop .pdpt_loop

    mov edi, 0xA02000
    mov ebx, 0x83
    mov ecx, 4 * 512
.pd_loop:
    mov [edi], ebx
    mov dword [edi + 4], 0
    add ebx, 0x200000
    add edi, 8
    loop .pd_loop

    mov dx, SERIAL
    mov al, 'd'
    out dx, al

    ; CR3 = база PML4. Без этого page walk идёт по мусору от BIOS.
    mov eax, PML4_ADDR
    mov cr3, eax

    ; PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov dx, SERIAL
    mov al, 'e'
    out dx, al

    ; EFER.LME
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov dx, SERIAL
    mov al, 'f'
    out dx, al

    ; Paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    mov dx, SERIAL
    mov al, 'g'
    out dx, al

    lgdt [0x8000 + gdt_ptr]
    jmp 0x18:(lm_entry + 0x8000)

[bits 64]
lm_entry:
    cld
    mov dx, SERIAL
    mov al, '4'
    out dx, al

    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x9F0000

    ; boot_info -> 0x8800
    mov rdi, 0x8800
    mov eax, [0x8000 + fb_addr]        ; 32 бита, eax обнуляет старшие биты rax
    mov [rdi], rax
    movzx eax, word [0x8000 + fb_width]
    mov [rdi + 8], eax
    movzx eax, word [0x8000 + fb_height]
    mov [rdi + 12], eax
    movzx eax, word [0x8000 + fb_pitch]
    mov [rdi + 16], eax
    movzx eax, word [0x8000 + e820_count]
    mov [rdi + 20], ax
    movzx eax, word [0x8000 + fb_bpp]
    mov [rdi + 22], ax

    mov rax, KERNEL_DST
    call rax

    hlt
    jmp $

; --- данные ---
fb_addr:    dd 0
fb_pitch:   dw 0
fb_width:   dw 0
fb_height:  dw 0
e820_count: dw 0
fb_bpp:     dw 0

align 8
gdt64:
    dq 0
    dq 0x00CF9A000000FFFF   ; 0x08 code32
    dq 0x00CF92000000FFFF   ; 0x10 data32
    dq 0x00209A0000000000   ; 0x18 code64
    dq 0x0000920000000000   ; 0x20 data64
gdt64_end:
gdt_ptr:
    dw gdt64_end - gdt64 - 1
gdt_base:
    dd 0
