.option norvc
.option norelax

.section .text
.align 4
.globl riscv_context_switch
riscv_context_switch:
    addi sp, sp, -64
    sw ra, 0(sp)
    sw s0, 4(sp)
    sw s1, 8(sp)
    sw s2, 12(sp)
    sw s3, 16(sp)
    sw s4, 20(sp)
    sw s5, 24(sp)
    sw s6, 28(sp)
    sw s7, 32(sp)
    sw s8, 36(sp)
    sw s9, 40(sp)
    sw s10, 44(sp)
    sw s11, 48(sp)
    sw sp, 0(a0)
    mv sp, a1
    lw ra, 0(sp)
    lw s0, 4(sp)
    lw s1, 8(sp)
    lw s2, 12(sp)
    lw s3, 16(sp)
    lw s4, 20(sp)
    lw s5, 24(sp)
    lw s6, 28(sp)
    lw s7, 32(sp)
    lw s8, 36(sp)
    lw s9, 40(sp)
    lw s10, 44(sp)
    lw s11, 48(sp)
    addi sp, sp, 64
    ret

.globl riscv_user_enter
riscv_user_enter:
    csrw mepc, a0
    csrw mstatus, a1
    mv sp, a2
    mv s0, a3
    mv s1, a4
    mret
1:
    j 1b

.globl rv32_exit
rv32_exit:
    li a7, 93
    ecall
2:
    j 2b
