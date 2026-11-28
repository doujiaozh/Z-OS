#include "riscv.h"
#include "hal/hal.h"
#include "lib.h"
#include "version.h"

void riscv_kernel_selftest(void);

const char *riscv_exit_tag(void) {
    return "z-os: riscv32 boot complete";
}

void kmain(void) {
    hal_console_write("");
    kprintf("\n");
    kprintf("Z-OS %s %s\n", Z_OS_VERSION, Z_OS_CODENAME);
    kprintf("HAL: riscv32 (rv32ima_zicsr)\n");
    kprintf("Boot: SV32 paging enabled, machine mode\n");

    if (!hal_cpu_verify()) {
        kprintf("PANIC: riscv32 paging state is invalid\n");
        rv32_exit(1);
    }
    kprintf("Verified: satp MODE=SV32, mtvec installed\n");

    kprintf("Running kernel self-test\n");
    riscv_kernel_selftest();

    kprintf("z-os: boot banner OK\n");
    kprintf("z-os: kernel self-test complete\n");
    rv32_exit(0);
}
