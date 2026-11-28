#include "hal/hal.h"
#include "riscv.h"
#include "lib.h"

static void rv32_early_init(void) {
}

static void rv32_console_init(void) {
}

static void rv32_console_putc(char character) {
    uart_putc(character);
}

static void rv32_console_write(const char *text) {
    uart_write(text);
}

static void rv32_console_set_color(u8 foreground, u8 background) {
    (void)foreground;
    (void)background;
}

static void rv32_console_reset_color(void) {
}

static void rv32_trap_init(void) {
    rv32_csr_write(RV32_CSR_MTVEC, (u32)(usize)trap_entry);
}

static bool rv32_cpu_verify(void) {
    u32 satp = rv32_csr_read(RV32_CSR_SATP);
    u32 mtvec = rv32_csr_read(RV32_CSR_MTVEC);
    if ((satp & RV32_SATP_MODE) == 0) {
        return false;
    }
    if (mtvec == 0) {
        return false;
    }
    return sv32_is_enabled();
}

static u64 rv32_mmu_root(void) {
    return sv32_root_phys();
}

static void rv32_mmu_switch(u64 root) {
    (void)root;
    rv32_csr_write(RV32_CSR_SATP, sv32_satp_value());
}

static bool rv32_mmu_enabled(void) {
    return sv32_is_enabled();
}

static void rv32_irq_enable(void) {
    rv32_csr_set(RV32_CSR_MSTATUS, RV32_MSTATUS_MIE);
}

static void rv32_irq_disable(void) {
    rv32_csr_clear(RV32_CSR_MSTATUS, RV32_MSTATUS_MIE);
}

static u64 rv32_irq_save(void) {
    u32 status = rv32_csr_read(RV32_CSR_MSTATUS);
    rv32_csr_clear(RV32_CSR_MSTATUS, RV32_MSTATUS_MIE);
    return (u64)(status & RV32_MSTATUS_MIE);
}

static void rv32_irq_restore(u64 flags) {
    if ((flags & RV32_MSTATUS_MIE) != 0) {
        rv32_irq_enable();
    } else {
        rv32_irq_disable();
    }
}

static void rv32_context_switch(u64 *old_stack, usize new_stack) {
    riscv_context_switch((u32 *)old_stack, (u32)new_stack);
}

static u64 rv32_ticks(void) {
    return (u64)timer_tick_count();
}

static void rv32_idle(void) {
    __asm__ volatile("wfi");
}

static void rv32_halt(void) {
    rv32_irq_disable();
    rv32_exit(0);
}

static const struct hal_operations rv32_operations = {
    HAL_ARCH_RISCV32,
    "riscv32",
    "rv32ima_zicsr",
    rv32_early_init,
    rv32_console_init,
    rv32_console_putc,
    rv32_console_write,
    rv32_console_set_color,
    rv32_console_reset_color,
    rv32_trap_init,
    rv32_cpu_verify,
    rv32_mmu_root,
    rv32_mmu_switch,
    rv32_mmu_enabled,
    rv32_irq_enable,
    rv32_irq_disable,
    rv32_irq_save,
    rv32_irq_restore,
    rv32_context_switch,
    rv32_ticks,
    rv32_idle,
    rv32_halt
};

const struct hal_operations *hal_platform(void) {
    return &rv32_operations;
}
