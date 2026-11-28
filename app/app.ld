ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x2000000;
    .text   : { *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    .myos.meta : { KEEP(*(.myos.meta)) } :text
    . = ALIGN(0x1000);
    .data   : { *(.data*) } :data
    .myos.imports : { KEEP(*(.myos.imports)) } :data
    .bss    : { *(.bss*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.eh_frame) }
}
