BITS 64

section .text

%macro STUB_NOERR 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro STUB_ERR 1
global isr%1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

STUB_NOERR 0
STUB_NOERR 1
STUB_NOERR 2
STUB_NOERR 3
STUB_NOERR 4
STUB_NOERR 5
STUB_NOERR 6
STUB_NOERR 7
STUB_ERR 8
STUB_NOERR 9
STUB_ERR 10
STUB_ERR 11
STUB_ERR 12
STUB_ERR 13
STUB_ERR 14
STUB_NOERR 15
STUB_NOERR 16
STUB_ERR 17
STUB_NOERR 18
STUB_NOERR 19
STUB_NOERR 20
STUB_ERR 21
STUB_NOERR 22
STUB_NOERR 23
STUB_NOERR 24
STUB_NOERR 25
STUB_NOERR 26
STUB_NOERR 27
STUB_NOERR 28
STUB_ERR 29
STUB_ERR 30
STUB_NOERR 31

%assign vec 32
%rep 96
STUB_NOERR vec
%assign vec vec+1
%endrep

STUB_NOERR 128

%assign vec 129
%rep 127
STUB_NOERR vec
%assign vec vec+1
%endrep

extern interrupt_dispatch
extern sched_interrupt_return

global isr_common
isr_common:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov ax, 0x10
    mov ds, ax
    mov es, ax

    mov rdi, rsp
    call interrupt_dispatch

    mov rdi, rsp
    call sched_interrupt_return
    test rax, rax
    jz .no_switch
    mov rsp, rax
.no_switch:

    test byte [rsp + 144], 3
    jz .kernel_data
    mov ax, 0x23
    jmp .set_data
.kernel_data:
    mov ax, 0x10
.set_data:
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

section .rodata
align 8
global isr_stub_table
isr_stub_table:
%assign vec 0
%rep 256
    dq isr%+vec
%assign vec vec+1
%endrep
