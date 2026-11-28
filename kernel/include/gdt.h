#ifndef Z_OS_GDT_H
#define Z_OS_GDT_H

#include "types.h"

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE 0x18
#define GDT_USER_DATA 0x20
#define GDT_TSS 0x28

struct tss {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} packed;

void gdt_init(void);
u64 gdt_base_address(void);
u64 tss_base_address(void);
void tss_set_rsp0(u64 stack);
u64 tss_get_rsp0(void);
void tss_set_ist1(u64 stack);

#endif
