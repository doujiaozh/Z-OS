.option norvc
.option norelax

.section .text.boot,"ax",@progbits
.align 4
.globl _start
_start:
    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:
    la sp, kernel_stack_top
    andi sp, sp, -16
    la t0, trap_entry
    csrw mtvec, t0
    call sv32_boot_init
    csrw satp, a0
    la t0, kmain
    jalr zero, 0(t0)
3:
    j 3b

.section .bss,"aw",@nobits
.align 4
.globl kernel_stack
kernel_stack:
    .space 32768
.globl kernel_stack_top
kernel_stack_top:

.align 4
.globl trap_stack
trap_stack:
    .space 16384
.globl trap_stack_top
trap_stack_top:

.align 4
.globl trap_scratch
trap_scratch:
    .space 64
