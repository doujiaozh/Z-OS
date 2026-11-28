#include "riscv.h"

u32 rv32_csr_read(u32 csr) {
    u32 value = 0;
    switch (csr) {
    case RV32_CSR_MSTATUS:
        __asm__ volatile("csrr %0, mstatus" : "=r"(value));
        break;
    case RV32_CSR_MIE:
        __asm__ volatile("csrr %0, mie" : "=r"(value));
        break;
    case RV32_CSR_MTVEC:
        __asm__ volatile("csrr %0, mtvec" : "=r"(value));
        break;
    case RV32_CSR_MEPC:
        __asm__ volatile("csrr %0, mepc" : "=r"(value));
        break;
    case RV32_CSR_MCAUSE:
        __asm__ volatile("csrr %0, mcause" : "=r"(value));
        break;
    case RV32_CSR_MIP:
        __asm__ volatile("csrr %0, mip" : "=r"(value));
        break;
    case RV32_CSR_SATP:
        __asm__ volatile("csrr %0, satp" : "=r"(value));
        break;
    default:
        break;
    }
    return value;
}

void rv32_csr_write(u32 csr, u32 value) {
    switch (csr) {
    case RV32_CSR_MSTATUS:
        __asm__ volatile("csrw mstatus, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_MIE:
        __asm__ volatile("csrw mie, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_MTVEC:
        __asm__ volatile("csrw mtvec, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_MEPC:
        __asm__ volatile("csrw mepc, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_MTVAL:
        __asm__ volatile("csrw mtval, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_MIP:
        __asm__ volatile("csrw mip, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_SATP:
        __asm__ volatile("csrw satp, %0" : : "r"(value) : "memory");
        break;
    default:
        break;
    }
}

void rv32_csr_set(u32 csr, u32 value) {
    switch (csr) {
    case RV32_CSR_MSTATUS:
        __asm__ volatile("csrs mstatus, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_MIE:
        __asm__ volatile("csrs mie, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_MIP:
        __asm__ volatile("csrs mip, %0" : : "r"(value) : "memory");
        break;
    default:
        break;
    }
}

void rv32_csr_clear(u32 csr, u32 value) {
    switch (csr) {
    case RV32_CSR_MSTATUS:
        __asm__ volatile("csrc mstatus, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_MIE:
        __asm__ volatile("csrc mie, %0" : : "r"(value) : "memory");
        break;
    case RV32_CSR_MIP:
        __asm__ volatile("csrc mip, %0" : : "r"(value) : "memory");
        break;
    default:
        break;
    }
}
