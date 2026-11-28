#include "hal/hal.h"

enum hal_arch hal_arch_id(void) {
    return hal_platform()->arch;
}

const char *hal_name(void) {
    return hal_platform()->name;
}

const char *hal_cpu_name(void) {
    return hal_platform()->cpu;
}

void hal_early_init(void) {
    hal_platform()->early_init();
}

void hal_console_init(void) {
    hal_platform()->console_init();
}

void hal_console_putc(char character) {
    hal_platform()->console_putc(character);
}

void hal_console_write(const char *text) {
    hal_platform()->console_write(text);
}

void hal_console_set_color(u8 foreground, u8 background) {
    hal_platform()->console_set_color(foreground, background);
}

void hal_console_reset_color(void) {
    hal_platform()->console_reset_color();
}

void hal_trap_init(void) {
    hal_platform()->trap_init();
}

bool hal_cpu_verify(void) {
    return hal_platform()->cpu_verify();
}

u64 hal_mmu_root(void) {
    return hal_platform()->mmu_root();
}

void hal_mmu_switch(u64 root) {
    hal_platform()->mmu_switch(root);
}

bool hal_mmu_enabled(void) {
    return hal_platform()->mmu_enabled();
}

void hal_irq_enable(void) {
    hal_platform()->irq_enable();
}

void hal_irq_disable(void) {
    hal_platform()->irq_disable();
}

u64 hal_irq_save(void) {
    return hal_platform()->irq_save();
}

void hal_irq_restore(u64 flags) {
    hal_platform()->irq_restore(flags);
}

void hal_context_switch(u64 *old_stack, usize new_stack) {
    hal_platform()->context_switch(old_stack, new_stack);
}

u64 hal_ticks(void) {
    return hal_platform()->ticks();
}

void hal_idle(void) {
    hal_platform()->idle();
}

void hal_halt(void) {
    hal_platform()->halt();
    for (;;) {
    }
}
