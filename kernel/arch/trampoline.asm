BITS 64

global kpti_trampoline_start
global kpti_trampoline_end
global kpti_probe_entry
global kpti_probe_switch
global kpti_probe_enter
global kpti_return_to_user
global kpti_return_cr3_slot
global kpti_kernel_cr3_slot
global kpti_probe_hit
global kpti_probe_seen
global kpti_probe_stack
global kpti_probe_stack_top
global kpti_stub_table
global kpti_return_stack
global kpti_return_stack_top

extern isr_common

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

kpti_return_to_user:
    test byte [rsp + 8], 3
    jz .no_view
    mov rax, [rel kpti_return_cr3_slot]
    test rax, rax
    jz .no_view
    push rax
    mov rax, [rel kpti_return_cr3_slot]
    mov cr3, rax
    pop rax
.no_view:
    iretq

%macro KPTI_SWITCH 0
    mov rax, [rel kpti_kernel_cr3_slot]
    mov rbx, cr3
    cmp rax, rbx
    je %%same
    mov cr3, rax
%%same:
%endmacro

%macro KPTI_STUB_NOERR 1
kpti_stub_%1:
    KPTI_SWITCH
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro KPTI_STUB_ERR 1
kpti_stub_%1:
    KPTI_SWITCH
    push qword %1
    jmp isr_common
%endmacro

KPTI_STUB_NOERR 0
KPTI_STUB_NOERR 1
KPTI_STUB_NOERR 2
KPTI_STUB_NOERR 3
KPTI_STUB_NOERR 4
KPTI_STUB_NOERR 5
KPTI_STUB_NOERR 6
KPTI_STUB_NOERR 7
KPTI_STUB_ERR 8
KPTI_STUB_NOERR 9
KPTI_STUB_ERR 10
KPTI_STUB_ERR 11
KPTI_STUB_ERR 12
KPTI_STUB_ERR 13
KPTI_STUB_ERR 14
KPTI_STUB_NOERR 15
KPTI_STUB_NOERR 16
KPTI_STUB_ERR 17
KPTI_STUB_NOERR 18
KPTI_STUB_NOERR 19
KPTI_STUB_NOERR 20
KPTI_STUB_ERR 21
KPTI_STUB_NOERR 22
KPTI_STUB_NOERR 23
KPTI_STUB_NOERR 24
KPTI_STUB_NOERR 25
KPTI_STUB_NOERR 26
KPTI_STUB_NOERR 27
KPTI_STUB_NOERR 28
KPTI_STUB_ERR 29
KPTI_STUB_ERR 30
KPTI_STUB_NOERR 31

%assign svec 32
%rep 96
KPTI_STUB_NOERR svec
%assign svec svec+1
%endrep

KPTI_STUB_NOERR 128

%assign svec 129
%rep 127
KPTI_STUB_NOERR svec
%assign svec svec+1
%endrep

kpti_trampoline_end:

section .kptidata progbits alloc nowrite align=64

kpti_kernel_cr3_slot:
    dq 0

kpti_return_cr3_slot:
    dq 0

kpti_probe_hit:
    dq 0

kpti_probe_seen:
    dq 0

global kpti_entry_counter
global kpti_return_counter

kpti_entry_counter:
    dq 0

kpti_return_counter:
    dq 0

kpti_stub_table:
%assign stvec 0
%rep 256
    dq kpti_stub_%+stvec
%assign stvec stvec+1
%endrep

align 16
kpti_return_stack:
    times 4096 db 0
kpti_return_stack_top:

align 16
kpti_probe_stack:
    times 4096 db 0
kpti_probe_stack_top:
