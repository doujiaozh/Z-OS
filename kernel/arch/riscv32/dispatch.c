#include "riscv.h"
#include "lib.h"

static void print_hex(u32 value) {
    kprintf("0x%08x", value);
}

static bool fault_is_expected(u32 cause) {
    return cause == RV32_CAUSE_LOAD_PAGE_FAULT || cause == RV32_CAUSE_STORE_PAGE_FAULT ||
           cause == RV32_CAUSE_INST_PAGE_FAULT;
}

void trap_dispatch(struct trap_frame *frame) {
    u32 cause = frame->mcause;
    u32 code = cause & RV32_CAUSE_MASK;

    if ((cause & RV32_CAUSE_INTERRUPT) != 0) {
        if (code == RV32_IRQ_TIMER) {
            rv32_tests.timer_traps++;
            timer_ack();
            return;
        }
        rv32_tests.unexpected_traps++;
        return;
    }

    if (fault_is_expected(code)) {
        rv32_tests.fault_count++;
        rv32_tests.fault_expected = 1;
        frame->mepc = 0;
        return;
    }

    if (code == RV32_CAUSE_ECALL_U) {
        u32 value = frame->x[10];
        u32 magic = frame->x[17];
        if (magic != RV32_ECALL_MAGIC) {
            rv32_tests.unexpected_traps++;
            return;
        }
        if (value == RV32_ECALL_STAGE_ONE) {
            rv32_tests.user_stage = 1;
            rv32_tests.user_value = frame->x[11];
            return;
        }
        if (value == RV32_ECALL_STAGE_TWO) {
            rv32_tests.user_stage = 2;
            rv32_tests.user_value = frame->x[11];
            return;
        }
        rv32_tests.unexpected_traps++;
        return;
    }

    if (code == RV32_CAUSE_ILLEGAL || code == RV32_CAUSE_ECALL_M) {
        rv32_tests.unexpected_traps++;
        print_hex(cause);
        kprintf("\n");
        return;
    }

    rv32_tests.unexpected_traps++;
    print_hex(cause);
    kprintf("\n");
}
