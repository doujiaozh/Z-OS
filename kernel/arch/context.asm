BITS 64

section .text
global context_switch
global thread_trampoline
extern sched_thread_bootstrap
extern sched_thread_exit

context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, rsi
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

thread_trampoline:
    and rsp, -16
    call sched_thread_bootstrap
    call sched_thread_exit
.hang:
    cli
    hlt
    jmp .hang
