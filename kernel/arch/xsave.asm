BITS 64

section .text
global arch_xsave
global arch_xrstor

arch_xsave:
    mov eax, 7
    xor edx, edx
    xsave [rdi]
    ret

arch_xrstor:
    mov eax, 7
    xor edx, edx
    xrstor [rdi]
    ret
