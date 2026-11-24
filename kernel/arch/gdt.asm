BITS 64

section .text
global gdt_load

gdt_load:
    lgdt [rdi]
    push qword 0x08
    lea rax, [rel .reload]
    push rax
    db 0x48, 0xCB
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
