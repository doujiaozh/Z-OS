OUTPUT_FORMAT(elf64-x86-64)
ENTRY(z_start)

SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

    .text : ALIGN(16)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    .trampoline : ALIGN(0x1000)
    {
        __trampoline_start = .;
        *(.trampoline)
        __trampoline_end = .;
    }

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(0x1000)
    {
        __data_start = .;
        *(.data .data.*)
        *(.kptidata)
    }

    .bss : ALIGN(0x1000)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
