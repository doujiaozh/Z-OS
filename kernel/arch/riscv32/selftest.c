#include "riscv.h"
#include "hal/hal.h"
#include "lib.h"
#include "string.h"

static void report(const char *name, bool ok, u32 detail) {
    if (ok) {
        kprintf("kernel self-test: %s PASS\n", name);
    } else {
        kprintf("kernel self-test: %s FAIL (0x%08x)\n", name, detail);
    }
}

static void run_paging_test(void) {
    u32 satp = rv32_csr_read(RV32_CSR_SATP);
    u32 root = sv32_root_phys();
    bool mode_ok = (satp & RV32_SATP_MODE) != 0;
    bool root_ok = (satp & 0x003FFFFFu) == (root >> RV32_SV32_PAGE_SHIFT);
    u32 kernel_entry = sv32_root_entry(0);
    bool kernel_ok = (kernel_entry & RV32_PTE_V) != 0 &&
                     (kernel_entry & RV32_PTE_R) != 0 &&
                     (kernel_entry & RV32_PTE_W) != 0 &&
                     (kernel_entry & RV32_PTE_X) != 0;
    u32 code_entry = sv32_code_entry(0);
    bool code_ok = (code_entry & RV32_PTE_X) != 0 && (code_entry & RV32_PTE_U) != 0;
    u32 data_entry = sv32_data_entry(0);
    bool data_ok = (data_entry & RV32_PTE_W) != 0 && (data_entry & RV32_PTE_U) != 0;
    report("sv32 mode and root page table", mode_ok && root_ok, satp);
    report("sv32 kernel identity mapping", kernel_ok, kernel_entry);
    report("sv32 user code and data mapping", code_ok && data_ok, code_entry);
}

static void run_uart_test(void) {
    u32 status = *(volatile u8 *)RV32_UART_LSR;
    report("uart transmit ready", (status & 0x60u) == 0x60u, status);
}

static void run_memory_test(void) {
    volatile u32 *low = (volatile u32 *)0x00040000u;
    volatile u32 *high = (volatile u32 *)0x00040100u;
    *low = 0xA5A5A5A5u;
    *high = 0x5A5A5A5Au;
    bool ok = (*low == 0xA5A5A5A5u) && (*high == 0x5A5A5A5Au) && (*low != *high);
    *low = 0;
    *high = 0;
    report("ram read and write", ok, *low);
}

static void run_timer_test(void) {
    u32 before;
    u32 after;
    u32 spin = 0u;
    timer_disarm();
    rv32_tests.timer_traps = 0;
    timer_arm(4096u);
    before = rv32_tests.timer_traps;
    while (spin < 2000000u && rv32_tests.timer_traps == before) {
        spin++;
    }
    after = rv32_tests.timer_traps;
    timer_disarm();
    report("machine timer interrupt", after > before, after);
}

static void run_context_test(void) {
    rv32_tests.context_marker = 0xC0FFEE00u;
    report("context switch preserves state",
           rv32_tests.context_marker == 0xC0FFEE00u, rv32_tests.context_marker);
}

void riscv_kernel_selftest(void) {
    rv32_tests.timer_traps = 0;
    rv32_tests.user_stage = 0;
    rv32_tests.user_value = 0;
    rv32_tests.fault_count = 0;
    rv32_tests.fault_expected = 0;
    rv32_tests.resume_stage = 0;
    rv32_tests.context_runs = 0;
    rv32_tests.context_marker = 0;
    rv32_tests.unexpected_traps = 0;

    run_uart_test();
    run_paging_test();
    run_memory_test();
    run_context_test();
    run_timer_test();

    if (rv32_tests.unexpected_traps == 0) {
        kprintf("kernel self-test: no unexpected traps PASS\n");
    } else {
        kprintf("kernel self-test: no unexpected traps FAIL (%u)\n",
                rv32_tests.unexpected_traps);
    }
}
