ENTRY(_start)

MEMORY
{
    ram : ORIGIN = 0x00000000, LENGTH = 16M
}

SECTIONS
{
    .vectors : {
        *(.vectors)
    } > ram

    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > ram

    .data : {
        *(.data)
        *(.data.*)
    } > ram

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } > ram

    . = ALIGN(8);
    _end = .;
    _stack_top = ORIGIN(ram) + LENGTH(ram);
}
