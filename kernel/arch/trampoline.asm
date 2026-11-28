BITS 64

global kpti_trampoline_start
global kpti_trampoline_end
global kpti_probe_entry
global kpti_probe_switch
global kpti_probe_enter
global kpti_kernel_cr3_slot
global kpti_probe_hit
global kpti_probe_seen
global kpti_probe_stack
global kpti_probe_stack_top

section .trampoline progbits alloc exec nowrite align=4096

kpti_trampoline_start:

kpti_probe_switch:
    push r12
    push r13
    mov r12, rdi
    mov r13, rsi
    mov cr3, r12
    mov rax, cr3
    mov cr3, r13
    pop r13
    pop r12
    ret

kpti_probe_entry:
    mov r11, cr3
    mov r10, [rel kpti_kernel_cr3_slot]
    test r10, r10
    jz .no_switch
    mov cr3, r10
.no_switch:
    mov qword [rel kpti_probe_seen], r11
    mov qword [rel kpti_probe_hit], 1
    iretq

kpti_probe_enter:
    mov r12, rdi
    mov r13, rsi
    mov r14, rsp
    lea rsp, [rel kpti_probe_stack_top]
    and rsp, -16
    mov cr3, r12
    int 0x81
    mov cr3, r13
    mov rsp, r14
    ret

kpti_trampoline_end:

section .kptidata progbits alloc nowrite align=8

kpti_kernel_cr3_slot:
    dq 0

kpti_probe_hit:
    dq 0

kpti_probe_seen:
    dq 0

align 16
kpti_probe_stack:
    times 4096 db 0
kpti_probe_stack_top:
