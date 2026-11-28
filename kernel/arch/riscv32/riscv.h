#ifndef Z_OS_ARCH_RISCV32_H
#define Z_OS_ARCH_RISCV32_H

#include "types.h"

#define RV32_CSR_MSTATUS 0x300u
#define RV32_CSR_MIE 0x304u
#define RV32_CSR_MTVEC 0x305u
#define RV32_CSR_MEPC 0x341u
#define RV32_CSR_MCAUSE 0x342u
#define RV32_CSR_MTVAL 0x343u
#define RV32_CSR_MIP 0x344u
#define RV32_CSR_SATP 0x180u

#define RV32_SATP_MODE 0x80000000u

#define RV32_MSTATUS_MIE 0x00000008u
#define RV32_MSTATUS_MPIE 0x00000080u
#define RV32_MSTATUS_MPP 0x00001800u
#define RV32_MSTATUS_MPP_MACHINE 0x00001800u
#define RV32_MSTATUS_MPP_USER 0x00000000u

#define RV32_MIE_MTIE 0x00000080u
#define RV32_MIP_MTIP 0x00000080u

#define RV32_IRQ_TIMER 7u
#define RV32_CAUSE_INTERRUPT 0x80000000u
#define RV32_CAUSE_MASK 0x7FFFFFFFu
#define RV32_CAUSE_ILLEGAL 2u
#define RV32_CAUSE_BREAKPOINT 3u
#define RV32_CAUSE_ECALL_U 8u
#define RV32_CAUSE_ECALL_M 11u
#define RV32_CAUSE_INST_PAGE_FAULT 12u
#define RV32_CAUSE_LOAD_PAGE_FAULT 13u
#define RV32_CAUSE_STORE_PAGE_FAULT 15u

#define RV32_UART_THR 0x10000000u
#define RV32_UART_LSR 0x10000005u

#define RV32_CLINT_MTIMECMP 0x02004000u
#define RV32_CLINT_MTIME 0x0200BFF8u

#define RV32_PTE_V 0x001u
#define RV32_PTE_R 0x002u
#define RV32_PTE_W 0x004u
#define RV32_PTE_X 0x008u
#define RV32_PTE_U 0x010u
#define RV32_PTE_A 0x040u
#define RV32_PTE_D 0x080u

#define RV32_SV32_ENTRIES 1024u
#define RV32_SV32_PAGE_SHIFT 12u
#define RV32_SV32_MEGA_SHIFT 22u

#define RV32_USER_CODE_BASE 0x40000000u
#define RV32_USER_DATA_BASE 0x80000000u
#define RV32_USER_PROBE_BASE 0x00001000u

#define RV32_SV32_CODE_INDEX (RV32_USER_CODE_BASE >> RV32_SV32_MEGA_SHIFT)
#define RV32_SV32_DATA_INDEX (RV32_USER_DATA_BASE >> RV32_SV32_MEGA_SHIFT)

#define RV32_PTE_PPN(physical) (((u32)(physical) >> RV32_SV32_PAGE_SHIFT) << 10)

#define RV32_ECALL_MAGIC 0x5Au
#define RV32_ECALL_STAGE_ONE 0x5A5Au
#define RV32_ECALL_STAGE_TWO 0x3333u
#define RV32_ECALL_ABORT 0xEEEEu

struct trap_frame {
    u32 x[32];
    u32 mepc;
    u32 mstatus;
    u32 mcause;
    u32 reserved;
};

struct riscv_test_state {
    u32 timer_traps;
    u32 user_stage;
    u32 user_value;
    u32 fault_count;
    u32 fault_expected;
    u32 resume_stage;
    u32 context_runs;
    u32 context_marker;
    u32 unexpected_traps;
};

extern volatile struct riscv_test_state rv32_tests;

u32 rv32_csr_read(u32 csr);
void rv32_csr_write(u32 csr, u32 value);
void rv32_csr_set(u32 csr, u32 value);
void rv32_csr_clear(u32 csr, u32 value);

void uart_putc(char character);
void uart_write(const char *text);
u64 clint_mtime(void);
void timer_arm(u32 interval);
void timer_disarm(void);
void timer_ack(void);
u32 timer_tick_count(void);

u32 sv32_boot_init(void);
u32 sv32_satp_value(void);
u32 sv32_root_phys(void);
u32 sv32_code_table_phys(void);
u32 sv32_data_table_phys(void);
u32 sv32_user_data_phys(void);
u32 sv32_user_stack_phys(void);
u32 sv32_root_entry(u32 index);
u32 sv32_code_entry(u32 index);
u32 sv32_data_entry(u32 index);
u32 sv32_user_code_va(const void *symbol);
u32 sv32_user_data_va(void);
u32 sv32_user_probe_va(void);
u32 sv32_user_stack_top(void);
bool sv32_is_enabled(void);

void trap_entry(void);
void trap_dispatch(struct trap_frame *frame);
void rv32_exit(int code) z_noreturn;
void riscv_context_switch(u32 *old_stack, u32 new_stack);
void riscv_user_enter(u32 entry, u32 status, u32 stack, u32 data, u32 probe) z_noreturn;
void riscv_user_finish(void) z_noreturn;

void kmain(void) z_noreturn;
void riscv_context_selftest(void);
void riscv_kernel_selftest(void);
const char *riscv_exit_tag(void);

extern u8 __utext_start[];
extern u8 __utext_end[];
extern u8 kernel_stack[];
extern u8 kernel_stack_top[];
extern u8 trap_stack[];
extern u8 trap_stack_top[];
extern u8 trap_scratch[];
extern void u_mode_entry(void);
extern void u_resume(void);
extern void u_fault_load(void);

#endif
