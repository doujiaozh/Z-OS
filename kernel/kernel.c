#include "ata.h"
#include "boot.h"
#include "e1000.h"
#include "console.h"
#include "gdt.h"
#include "heap.h"
#include "kstack.h"
#include "protect.h"
#include "interrupts.h"
#include "io.h"
#include "kernel.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "lib.h"
#include "pmm.h"
#include "pic.h"
#include "process.h"
#include "rtc.h"
#include "sched.h"
#include "security.h"
#include "serial.h"
#include "shell.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "tests.h"
#include "vfs.h"
#include "vmm.h"
#include "wm.h"
#include "zfs.h"
#include "fb.h"

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

    u64 efer = read_msr(0xC0000080);
    u64 cr4 = read_cr4();
    if ((efer & 0x100) == 0 || (cr4 & 0x20) == 0) {
        panic("long mode state is invalid");
    }
    kprintf("Long mode: EFER.LME verified, CR4.PAE verified\n");
    security_init();
    security_init_canary();

    gdt_init();
    interrupts_init();
    kprintf("GDT, TSS, IDT, and PIC initialized\n");

    pmm_init(boot);
    vmm_init();
    heap_init();
    kstack_init();
    kernel_protect_init();
    vfs_init();
    ata_init();
    if (ata_present()) {
        kprintf("ATA: primary master, %u sectors, model %s\n", ata_sector_count(), ata_model());
    } else {
        kprintf("ATA: no primary master device\n");
    }
    net_init();
    if (net_ready()) {
        char mac_text[24];
        char ip_text[20];
        net_format_mac(net_config_get()->mac, mac_text, sizeof(mac_text));
        net_format_ip(net_config_get()->ip, ip_text, sizeof(ip_text));
        kprintf("net: e1000 %s, mac %s, ip %s, link %s\n",
                e1000_link_state(), mac_text, ip_text, e1000_link_state());
    } else {
        kprintf("net: no e1000 device\n");
    }
    process_init();
    sched_process_init();
    fb_init();
    wm_init();
    mouse_init();
    mouse_event_init();
    kernel_nx_sweep();
    sched_init();

    if (fb_device_found()) {
        kprintf("FB: vga device at %p, dispi %s\n", (void *)fb_pci_address(),
                fb_dispi_supported() ? "supported" : "unavailable");
    } else {
        kprintf("FB: no pci vga device detected\n");
    }

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
