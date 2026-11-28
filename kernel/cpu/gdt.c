#include "gdt.h"
#include "string.h"
#include "io.h"
#include "lib.h"

struct gdt_pointer {
    u16 limit;
    u64 base;
} packed;

static u64 gdt_entries[7] aligned(16);
static struct tss tss_entry aligned(16);
static struct gdt_pointer gdt_pointer;
static u8 double_fault_stack[8192] aligned(16);

extern void gdt_load(struct gdt_pointer *pointer);

static u64 make_descriptor(u32 base, u32 limit, u8 access, u8 granularity) {
    u64 descriptor = 0;
    descriptor |= (u64)(limit & 0xFFFF);
    descriptor |= (u64)(base & 0xFFFF) << 16;
    descriptor |= (u64)((base >> 16) & 0xFF) << 32;
    descriptor |= (u64)access << 40;
    descriptor |= (u64)((limit >> 16) & 0x0F) << 48;
    descriptor |= (u64)(granularity & 0xF0) << 48;
    descriptor |= (u64)((base >> 24) & 0xFF) << 56;
    return descriptor;
}

static u64 make_tss_descriptor(u64 base, u32 limit) {
    u64 descriptor = 0;
    descriptor |= (u64)(limit & 0xFFFF);
    descriptor |= (u64)(base & 0xFFFF) << 16;
    descriptor |= (u64)((base >> 16) & 0xFF) << 32;
    descriptor |= (u64)0x89 << 40;
    descriptor |= (u64)((limit >> 16) & 0x0F) << 48;
    descriptor |= (u64)((base >> 24) & 0xFF) << 56;
    return descriptor;
}

void tss_set_rsp0(u64 stack) {
    tss_entry.rsp0 = stack;
}

u64 tss_get_rsp0(void) {
    return tss_entry.rsp0;
}

void tss_set_ist1(u64 stack) {
    tss_entry.ist1 = stack;
}

u64 gdt_base_address(void) {
    return (u64)(usize)gdt_entries;
}

u64 tss_base_address(void) {
    return (u64)(usize)&tss_entry;
}

void gdt_init(void) {
    memset(gdt_entries, 0, sizeof(gdt_entries));
    memset(&tss_entry, 0, sizeof(tss_entry));
    tss_entry.iomap_base = (u16)sizeof(tss_entry);
    tss_entry.ist1 = (u64)double_fault_stack + sizeof(double_fault_stack);

    gdt_entries[0] = 0;
    gdt_entries[1] = make_descriptor(0, 0xFFFFF, 0x9A, 0xAF);
    gdt_entries[2] = make_descriptor(0, 0xFFFFF, 0x92, 0xCF);
    gdt_entries[3] = make_descriptor(0, 0xFFFFF, 0xFA, 0xAF);
    gdt_entries[4] = make_descriptor(0, 0xFFFFF, 0xF2, 0xCF);
    u64 tss_base = (u64)&tss_entry;
    gdt_entries[5] = make_tss_descriptor(tss_base, sizeof(tss_entry) - 1);
    gdt_entries[6] = tss_base >> 32;

    gdt_pointer.limit = (u16)(sizeof(gdt_entries) - 1);
    gdt_pointer.base = (u64)gdt_entries;
    gdt_load(&gdt_pointer);

    u16 tss_selector = GDT_TSS;
    __asm__ volatile("ltr %0" : : "r"(tss_selector));
}
