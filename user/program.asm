BITS 64

section .rodata
global user_program_start
global user_program_end

user_program_start:
    incbin "user_init.elf"
user_program_end:
