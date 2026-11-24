BITS 64

section .text
global user_enter_and_wait
global user_exit_to_kernel

user_enter_and_wait:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rel user_return_rsp], rsp
    call user_enter
.restore:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

user_exit_to_kernel:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov rsp, [rel user_return_rsp]
    jmp user_enter_and_wait.restore

user_enter:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    push qword 0x23
    push rsi
    push qword 0x202
    push qword 0x1B
    push rdi
    iretq

section .bss
align 8
user_return_rsp:
    resq 1
