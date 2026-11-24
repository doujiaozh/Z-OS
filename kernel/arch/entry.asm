BITS 64

section .text.entry
global z_start
extern kmain
extern __bss_start
extern __bss_end

z_start:
    cli
    cld
    mov r15, rdi

    mov rax, cr0
    mov rcx, ~0xC
    and rax, rcx
    or rax, 0x2
    mov cr0, rax
    mov rax, cr4
    or rax, 0x600
    mov cr4, rax
    fninit

    lea rdi, [rel __bss_start]
    lea rcx, [rel __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    lea rsp, [rel boot_stack_top]
    and rsp, -16
    xor ebp, ebp
    mov rdi, r15
    call kmain

.hang:
    cli
    hlt
    jmp .hang

section .bss
global boot_stack
global boot_stack_top
align 16
boot_stack:
    resb 65536
boot_stack_top:
