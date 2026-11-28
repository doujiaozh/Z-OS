BITS 64

section .text
global sched_enter_user
global sched_resume_user_frame
global sched_abandon_to_dispatcher


sched_enter_user:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    jmp sched_user_resume

sched_resume_user_frame:
    mov rsp, rdi
sched_user_resume:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

sched_abandon_to_dispatcher:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov rsp, rdi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
