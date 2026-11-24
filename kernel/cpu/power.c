#include "power.h"
#include "io.h"
#include "lib.h"

struct null_idt {
    u16 limit;
    u64 base;
} packed;

void system_reboot(void) {
    interrupts_disable();
    for (u32 spin = 0; spin < 100000; spin++) {
        if ((inb(0x64) & 0x02) == 0) {
            break;
        }
    }
    outb(0x64, 0xFE);
    for (u32 spin = 0; spin < 1000000; spin++) {
        cpu_pause();
    }
    struct null_idt idt = {0, 0};
    __asm__ volatile("lidt %0" : : "m"(idt));
    __asm__ volatile("int3");
    halt_forever();
}

void system_shutdown(void) {
    interrupts_disable();
    outw(0x604, 0x2000);
    for (u32 spin = 0; spin < 500000; spin++) {
        cpu_pause();
    }
    outw(0xB004, 0x2000);
    for (u32 spin = 0; spin < 100000; spin++) {
        cpu_pause();
    }
    outw(0x4004, 0x3400);
    kprintf("Shutdown is not supported by this platform; system halted.\n");
    halt_forever();
}

void qemu_debug_exit(u32 code) {
    interrupts_disable();
    outl(0xF4, code);
    halt_forever();
}
