#include "hal/hal.h"
#include "console.h"
#include "gdt.h"
#include "interrupts.h"
#include "io.h"
#include "serial.h"
#include "timer.h"

extern void context_switch(u64 *old_stack, u64 new_stack);

static void x86_early_init(void) {
    serial_init();
}

static void x86_console_init(void) {
    console_init();
}

static void x86_console_putc(char character) {
    console_putc(character);
}

static void x86_console_write(const char *text) {
    console_write(text);
}

static void x86_console_set_color(u8 foreground, u8 background) {
    console_set_color(foreground, background);
}

static void x86_console_reset_color(void) {
    console_reset_color();
}

static void x86_trap_init(void) {
    gdt_init();
    interrupts_init();
}

static bool x86_cpu_verify(void) {
    u64 efer = read_msr(0xC0000080);
    u64 cr4 = read_cr4();
    return (efer & 0x100) != 0 && (cr4 & 0x20) != 0;
}

static u64 x86_mmu_root(void) {
    return read_cr3();
}

static void x86_mmu_switch(u64 root) {
    write_cr3(root);
}

static bool x86_mmu_enabled(void) {
    return true;
}

static void x86_irq_enable(void) {
    interrupts_enable();
}

static void x86_irq_disable(void) {
    interrupts_disable();
}

static u64 x86_irq_save(void) {
    return irq_save();
}

static void x86_irq_restore(u64 flags) {
    irq_restore(flags);
}

static void x86_context_switch(u64 *old_stack, usize new_stack) {
    context_switch(old_stack, (u64)new_stack);
}

static u64 x86_ticks(void) {
    return timer_ticks();
}

static void x86_idle(void) {
    cpu_halt();
}

static void x86_halt(void) {
    interrupts_disable();
    for (;;) {
        cpu_halt();
    }
}

static const struct hal_operations x86_operations = {
    HAL_ARCH_X86_64,
    "x86_64",
    "long mode",
    x86_early_init,
    x86_console_init,
    x86_console_putc,
    x86_console_write,
    x86_console_set_color,
    x86_console_reset_color,
    x86_trap_init,
    x86_cpu_verify,
    x86_mmu_root,
    x86_mmu_switch,
    x86_mmu_enabled,
    x86_irq_enable,
    x86_irq_disable,
    x86_irq_save,
    x86_irq_restore,
    x86_context_switch,
    x86_ticks,
    x86_idle,
    x86_halt
};

const struct hal_operations *hal_platform(void) {
    return &x86_operations;
}
