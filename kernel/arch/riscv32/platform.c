#include "riscv.h"
#include "lib.h"

volatile struct riscv_test_state rv32_tests;

static volatile u32 timer_ticks = 0;
static volatile u32 timer_interval = 4000u;
static volatile u32 timer_active = 0;

void uart_putc(char character) {
    *(volatile u8 *)RV32_UART_THR = (u8)character;
}

void uart_write(const char *text) {
    if (text == NULL) {
        return;
    }
    while (*text != 0) {
        uart_putc(*text);
        text++;
    }
}

u64 clint_mtime(void) {
    u32 high = *(volatile u32 *)(RV32_CLINT_MTIME + 4u);
    u32 low = *(volatile u32 *)RV32_CLINT_MTIME;
    u32 confirm = *(volatile u32 *)(RV32_CLINT_MTIME + 4u);
    if (high != confirm) {
        high = confirm;
        low = *(volatile u32 *)RV32_CLINT_MTIME;
    }
    return ((u64)high << 32) | (u64)low;
}

static void mtimecmp_write(u64 value) {
    volatile u32 *compare = (volatile u32 *)RV32_CLINT_MTIMECMP;
    compare[0] = 0xFFFFFFFFu;
    compare[1] = (u32)(value >> 32);
    compare[0] = (u32)(value & 0xFFFFFFFFu);
}

void timer_arm(u32 interval) {
    timer_interval = interval;
    timer_ticks = 0;
    timer_active = 1;
    mtimecmp_write(clint_mtime() + (u64)interval);
    rv32_csr_set(RV32_CSR_MIE, RV32_MIE_MTIE);
    rv32_csr_set(RV32_CSR_MSTATUS, RV32_MSTATUS_MIE);
}

void timer_disarm(void) {
    timer_active = 0;
    rv32_csr_clear(RV32_CSR_MSTATUS, RV32_MSTATUS_MIE);
    rv32_csr_clear(RV32_CSR_MIE, RV32_MIE_MTIE);
    mtimecmp_write(~(u64)0);
}

void timer_ack(void) {
    timer_ticks++;
    mtimecmp_write(clint_mtime() + (u64)timer_interval);
}

u32 timer_tick_count(void) {
    return timer_ticks;
}

bool timer_is_active(void) {
    return timer_active != 0;
}
