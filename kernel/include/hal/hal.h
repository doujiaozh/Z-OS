#ifndef Z_OS_HAL_H
#define Z_OS_HAL_H

#include "types.h"

enum hal_arch {
    HAL_ARCH_UNKNOWN = 0,
    HAL_ARCH_X86_64 = 1,
    HAL_ARCH_RISCV32 = 2
};

struct hal_operations {
    enum hal_arch arch;
    const char *name;
    const char *cpu;
    void (*early_init)(void);
    void (*console_init)(void);
    void (*console_putc)(char character);
    void (*console_write)(const char *text);
    void (*console_set_color)(u8 foreground, u8 background);
    void (*console_reset_color)(void);
    void (*trap_init)(void);
    bool (*cpu_verify)(void);
    u64 (*mmu_root)(void);
    void (*mmu_switch)(u64 root);
    bool (*mmu_enabled)(void);
    void (*irq_enable)(void);
    void (*irq_disable)(void);
    u64 (*irq_save)(void);
    void (*irq_restore)(u64 flags);
    void (*context_switch)(u64 *old_stack, usize new_stack);
    u64 (*ticks)(void);
    void (*idle)(void);
    void (*halt)(void);
};

const struct hal_operations *hal_platform(void);

enum hal_arch hal_arch_id(void);
const char *hal_name(void);
const char *hal_cpu_name(void);
void hal_early_init(void);
void hal_console_init(void);
void hal_console_putc(char character);
void hal_console_write(const char *text);
void hal_console_set_color(u8 foreground, u8 background);
void hal_console_reset_color(void);
void hal_trap_init(void);
bool hal_cpu_verify(void);
u64 hal_mmu_root(void);
void hal_mmu_switch(u64 root);
bool hal_mmu_enabled(void);
void hal_irq_enable(void);
void hal_irq_disable(void);
u64 hal_irq_save(void);
void hal_irq_restore(u64 flags);
void hal_context_switch(u64 *old_stack, usize new_stack);
u64 hal_ticks(void);
void hal_idle(void);
void hal_halt(void) z_noreturn;

#endif
