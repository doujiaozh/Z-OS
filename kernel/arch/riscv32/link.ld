OUTPUT_FORMAT("elf32-littleriscv")
OUTPUT_ARCH("riscv")
ENTRY(_start)

SECTIONS
{
    . = 0x00000000;
    __kernel_start = .;

    .text : ALIGN(4)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    }

    .data : ALIGN(16)
    {
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        __global_pointer$ = . + 0x800;
    }

    . = ALIGN(0x1000);
    __utext_start = .;
    .utext : ALIGN(4)
    {
        KEEP(*(.utext))
    }
    __utext_content_end = .;
    . = __utext_start + 0x1000;
    __utext_end = .;

    . = ALIGN(16);
    __bss_start = .;
    .bss : ALIGN(16)
    {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
    }
    . = ALIGN(16);
    __bss_end = .;
    __kernel_end = .;

    ASSERT(__utext_content_end <= __utext_end, "user text exceeds one page")
    ASSERT(__kernel_end < 0x003F0000, "kernel image overlaps page tables")

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.riscv.attributes)
    }
}
