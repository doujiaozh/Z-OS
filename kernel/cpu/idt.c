#include "interrupts.h"
#include "gdt.h"
#include "pic.h"
#include "io.h"
#include "lib.h"
#include "console.h"
#include "e1000.h"
#include "keyboard.h"
#include "kpti.h"
#include "kstack.h"
#include "process.h"
#include "vmm.h"
#include "serial.h"
#include "timer.h"
#include "mouse.h"
#include "syscall.h"
#include "sched.h"

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attributes;
    u16 offset_middle;
    u32 offset_high;
    u32 reserved;
} packed;

struct idt_pointer {
    u16 limit;
    u64 base;
} packed;

static struct idt_entry idt[256] aligned(16);
static struct idt_pointer idt_pointer;
extern void *isr_stub_table[256];

static const char *exception_names[32] = {
    "divide error",
    "debug",
    "non-maskable interrupt",
    "breakpoint",
    "overflow",
    "bound range exceeded",
    "invalid opcode",
    "device not available",
    "double fault",
    "coprocessor segment overrun",
    "invalid TSS",
    "segment not present",
    "stack-segment fault",
    "general protection fault",
    "page fault",
    "reserved",
    "x87 floating-point exception",
    "alignment check",
    "machine check",
    "SIMD floating-point exception",
    "virtualization exception",
    "control protection exception",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "hypervisor injection exception",
    "VMM communication exception",
    "security exception",
    "reserved"
};

const char *exception_name(u64 vector) {
    if (vector < 32) {
        return exception_names[vector];
    }
    return "unknown";
}

static void idt_set_gate(u8 vector, u64 handler, u16 selector, u8 attributes, u8 ist) {
    struct idt_entry *entry = &idt[vector];
    entry->offset_low = (u16)(handler & 0xFFFF);
    entry->selector = selector;
    entry->ist = (u8)(ist & 0x07);
    entry->type_attributes = attributes;
    entry->offset_middle = (u16)((handler >> 16) & 0xFFFF);
    entry->offset_high = (u32)((handler >> 32) & 0xFFFFFFFF);
    entry->reserved = 0;
}

u64 idt_table_physical(void) {
    return vmm_get_physical((u64)(usize)idt);
}

u64 interrupt_idt_base(void) {
    return (u64)(usize)idt;
}

void interrupts_init(void) {
    for (u32 vector = 0; vector < 256; vector++) {
        idt_set_gate((u8)vector, (u64)isr_stub_table[vector], GDT_KERNEL_CODE, 0x8E,
                     vector == 8 ? 1 : 0);
    }
    idt_set_gate(0x80, (u64)isr_stub_table[0x80], GDT_KERNEL_CODE, 0xEE, 0);
    idt_set_gate(0x81, (u64)(usize)kpti_probe_entry, GDT_KERNEL_CODE, 0x8E, 0);
    idt_pointer.limit = (u16)(sizeof(idt) - 1);
    idt_pointer.base = (u64)idt;
    __asm__ volatile("lidt %0" : : "m"(idt_pointer));
    pic_remap();
    pic_mask_all();
}

static void exception_panic(struct registers *registers) {
    console_set_color(COLOR_WHITE, COLOR_RED);
    kprintf("\nEXCEPTION %llu: %s\n", registers->vector, exception_name(registers->vector));
    kprintf("error=%016llx rip=%016llx cs=%016llx rflags=%016llx\n",
            registers->error, registers->rip, registers->cs, registers->rflags);
    kprintf("rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n",
            registers->rax, registers->rbx, registers->rcx, registers->rdx);
    kprintf("rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n",
            registers->rsi, registers->rdi, registers->rbp, registers->rsp);
    if (registers->vector == 14) {
        kprintf("cr2=%016llx\n", read_cr2());
    }
    console_reset_color();
    halt_forever();
}

void interrupt_dispatch(struct registers *registers) {
    u64 vector = registers->vector;
    if (vector < 32) {
        if (vector == 14 && process_current() != NULL && kstack_guard_fault(read_cr2())) {
            kprintf("stack guard fault terminated process %llu\n", process_current()->pid);
            sched_terminate_current(0x120ULL);
            return;
        }
        if ((registers->cs & 3) == 3) {
            sched_terminate_current(0x100ULL | vector);
        }
        exception_panic(registers);
        return;
    }
    if (vector >= 32 && vector < 48) {
        u8 irq = (u8)(vector - 32);
        pic_send_eoi(irq);
        if (irq == PIC_IRQ_TIMER) {
            timer_irq_handler();
        } else if (irq == PIC_IRQ_KEYBOARD) {
            keyboard_irq_handler();
        } else if (irq == PIC_IRQ_SERIAL) {
            serial_irq_handler();
        } else if (irq == 12) {
            mouse_irq_handler();
        } else if (e1000_present() && irq == e1000_irq_line()) {
            e1000_irq_handler();
        }
        return;
    }
    if (vector == 0x80) {
        syscall_dispatch(registers);
        return;
    }
    kprintf("unexpected interrupt vector %llu\n", vector);
}
