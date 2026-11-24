#include "boot.h"
#include "console.h"
#include "gdt.h"
#include "heap.h"
#include "interrupts.h"
#include "io.h"
#include "kernel.h"
#include "keyboard.h"
#include "lib.h"
#include "pmm.h"
#include "pic.h"
#include "rtc.h"
#include "serial.h"
#include "shell.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "tests.h"
#include "vfs.h"
#include "vmm.h"

void kmain(struct boot_info *boot) {
    serial_init();
    console_init();
    console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    kprintf("Z-OS %s %s\n", Z_OS_VERSION, Z_OS_CODENAME);
    console_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    kprintf("64-bit kernel entered\n");
    if (boot == NULL || boot->magic != BOOTINFO_MAGIC) {
        panic("invalid boot information");
    }
    if (boot->version != BOOTINFO_VERSION) {
        panic("unsupported boot information version");
    }
    kprintf("BootInfo at %p, memory map entries: %u\n", boot, boot->entry_count);

    gdt_init();
    interrupts_init();
    kprintf("GDT, TSS, IDT, and PIC initialized\n");

    pmm_init(boot);
    vmm_init();
    heap_init();
    vfs_init();
    sched_init();

    timer_init(TIMER_FREQUENCY);
    keyboard_init();
    serial_enable_rx_interrupt();
    pic_set_mask(PIC_IRQ_SERIAL, false);

    interrupts_enable();
    kprintf("Interrupts enabled at %u Hz\n", timer_frequency());

    struct rtc_time time;
    rtc_read(&time);
    kprintf("RTC: %04u-%02u-%02u %02u:%02u:%02u\n", time.year, time.month, time.day,
            time.hour, time.minute, time.second);

#ifdef Z_SELFTEST
    if (sched_create_thread("selftest", selftest_thread, NULL) == 0) {
        panic("failed to create selftest thread");
    }
    kprintf("Self-test mode active.\n");
#else
    if (sched_create_thread("shell", shell_thread, NULL) == 0) {
        panic("failed to create shell thread");
    }
    kprintf("Boot complete. Starting shell.\n");
#endif
    sched_start();
}
