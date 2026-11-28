.option norvc
.option norelax

.section .utext,"ax",@progbits
.align 4
.globl u_mode_entry
u_mode_entry:
    li a0, 0x1234
    sw a0, 0(s0)
    lw a1, 0(s0)
    bne a0, a1, u_abort
    li a0, 0x5678
    sw a0, 4(s0)
    lw a1, 4(s0)
    bne a0, a1, u_abort
    li a0, 0x5a5a
    li a7, 0x5a
    ecall
u_fault_load:
    lw a1, 0(s1)
    li a0, 0x7777
    li a7, 0x5a
    ecall
u_abort:
    li a0, 0xeeee
    li a7, 0x5a
    ecall
1:
    j 1b
