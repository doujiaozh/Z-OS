#include "riscv.h"
#include "lib.h"

#define RV32_ROOT_PHYS 0x00100000u
#define RV32_CODE_TABLE_PHYS 0x00101000u
#define RV32_DATA_TABLE_PHYS 0x00102000u
#define RV32_USER_DATA_PHYS 0x000F0000u
#define RV32_USER_STACK_PHYS 0x000F1000u

#define RV32_FLAG_MEGA (RV32_PTE_V | RV32_PTE_R | RV32_PTE_W | RV32_PTE_X | \
                        RV32_PTE_A | RV32_PTE_D)
#define RV32_FLAG_TABLE RV32_PTE_V
#define RV32_FLAG_USER_CODE (RV32_PTE_V | RV32_PTE_R | RV32_PTE_X | RV32_PTE_U | \
                             RV32_PTE_A | RV32_PTE_D)
#define RV32_FLAG_USER_DATA (RV32_PTE_V | RV32_PTE_R | RV32_PTE_W | RV32_PTE_U | \
                             RV32_PTE_A | RV32_PTE_D)

static volatile u32 active_satp = 0;

u32 sv32_boot_init(void) {
    volatile u32 *root = (volatile u32 *)RV32_ROOT_PHYS;
    volatile u32 *code = (volatile u32 *)RV32_CODE_TABLE_PHYS;
    volatile u32 *data = (volatile u32 *)RV32_DATA_TABLE_PHYS;
    volatile u32 *udata = (volatile u32 *)RV32_USER_DATA_PHYS;
    volatile u32 *ustack = (volatile u32 *)RV32_USER_STACK_PHYS;
    u32 index;

    for (index = 0; index < RV32_SV32_ENTRIES; index++) {
        root[index] = 0;
        code[index] = 0;
        data[index] = 0;
    }
    for (index = 0; index < 1024u; index++) {
        udata[index] = 0;
        ustack[index] = 0;
    }

    root[0] = RV32_PTE_PPN(0u) | RV32_FLAG_MEGA;
    root[RV32_SV32_CODE_INDEX] = RV32_PTE_PPN(RV32_CODE_TABLE_PHYS) | RV32_FLAG_TABLE;
    root[RV32_SV32_DATA_INDEX] = RV32_PTE_PPN(RV32_DATA_TABLE_PHYS) | RV32_FLAG_TABLE;

    code[0] = RV32_PTE_PPN((u32)(usize)__utext_start) | RV32_FLAG_USER_CODE;
    data[0] = RV32_PTE_PPN(RV32_USER_DATA_PHYS) | RV32_FLAG_USER_DATA;
    data[1] = RV32_PTE_PPN(RV32_USER_STACK_PHYS) | RV32_FLAG_USER_DATA;

    active_satp = RV32_SATP_MODE | (RV32_ROOT_PHYS >> RV32_SV32_PAGE_SHIFT);
    return active_satp;
}

u32 sv32_satp_value(void) {
    return active_satp;
}

u32 sv32_root_phys(void) {
    return RV32_ROOT_PHYS;
}

u32 sv32_code_table_phys(void) {
    return RV32_CODE_TABLE_PHYS;
}

u32 sv32_data_table_phys(void) {
    return RV32_DATA_TABLE_PHYS;
}

u32 sv32_user_data_phys(void) {
    return RV32_USER_DATA_PHYS;
}

u32 sv32_user_stack_phys(void) {
    return RV32_USER_STACK_PHYS;
}

u32 sv32_root_entry(u32 index) {
    if (index >= RV32_SV32_ENTRIES) {
        return 0;
    }
    return ((volatile u32 *)RV32_ROOT_PHYS)[index];
}

u32 sv32_code_entry(u32 index) {
    if (index >= RV32_SV32_ENTRIES) {
        return 0;
    }
    return ((volatile u32 *)RV32_CODE_TABLE_PHYS)[index];
}

u32 sv32_data_entry(u32 index) {
    if (index >= RV32_SV32_ENTRIES) {
        return 0;
    }
    return ((volatile u32 *)RV32_DATA_TABLE_PHYS)[index];
}

u32 sv32_user_code_va(const void *symbol) {
    return RV32_USER_CODE_BASE + ((u32)(usize)symbol - (u32)(usize)__utext_start);
}

u32 sv32_user_data_va(void) {
    return RV32_USER_DATA_BASE;
}

u32 sv32_user_probe_va(void) {
    return RV32_USER_PROBE_BASE;
}

u32 sv32_user_stack_top(void) {
    return RV32_USER_DATA_BASE + 0x2000u - 16u;
}

bool sv32_is_enabled(void) {
    return rv32_csr_read(RV32_CSR_SATP) == active_satp;
}
