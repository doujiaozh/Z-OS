BITS 64

section .rodata
global user_program_start
global user_program_end

user_program_start:
    incbin "user_hello.bin"
user_program_end:
