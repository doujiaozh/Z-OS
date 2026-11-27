#include "protect.h"
#include "io.h"
#include "lib.h"
#include "pmm.h"
#include "string.h"
#include "vmm.h"

extern u8 __kernel_start[];
extern u8 __data_start[];
extern u8 __bss_start[];
extern u8 __kernel_end[];

static bool protect_active = false;
static u32 protected_pages = 0;

static u64 align_up(u64 value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

void kernel_protect_init(void) {
    u64 kernel_start = (u64)(usize)__kernel_start;
    u64 data_start = (u64)(usize)__data_start;
    u64 kernel_end = align_up((u64)(usize)__kernel_end);
    u64 *pml4 = (u64 *)vmm_pml4_phys();
    if ((pml4[0] & PTE_PRESENT) == 0) {
        return;
    }
    u64 *pdpt = (u64 *)(pml4[0] & PTE_ADDRESS);
    if ((pdpt[0] & PTE_PRESENT) == 0 || (pdpt[0] & PTE_HUGE) != 0) {
        return;
    }
    u64 *pd = (u64 *)(pdpt[0] & PTE_ADDRESS);
    u64 kernel_limit = align_up(kernel_end);
    u32 first_pd = 0;
    u32 last_pd = (u32)((kernel_limit + 0x200000ULL - 1) / 0x200000ULL);
    if (last_pd > 512) {
        last_pd = 512;
    }
    for (u32 pd_index = first_pd; pd_index < last_pd; pd_index++) {
        u64 entry = pd[pd_index];
        if ((entry & PTE_PRESENT) == 0 || (entry & PTE_HUGE) == 0) {
            continue;
        }
        u64 page_table_frame = pmm_alloc_frame();
        if (page_table_frame == 0) {
            return;
        }
        u64 *page_table = (u64 *)page_table_frame;
        memset(page_table, 0, PAGE_SIZE);
        u64 base = entry & PTE_ADDRESS;
        for (u32 index = 0; index < 512; index++) {
            u64 page_address = base + (u64)index * PAGE_SIZE;
            u64 flags = PTE_PRESENT | PTE_WRITABLE;
            u64 data_page = data_start & ~(PAGE_SIZE - 1);
            if (page_address >= data_page && page_address < kernel_limit) {
                flags |= PTE_NO_EXECUTE;
                protected_pages++;
            } else if (page_address >= kernel_start && page_address + PAGE_SIZE <= data_page) {
                flags &= ~PTE_WRITABLE;
                protected_pages++;
            }
            page_table[index] = page_address | flags;
        }
        pd[pd_index] = page_table_frame | PTE_PRESENT | PTE_WRITABLE;
    }
    write_cr3(vmm_pml4_phys());
    protect_active = true;
}

bool kernel_protect_active(void) {
    return protect_active;
}

u32 kernel_protected_pages(void) {
    return protected_pages;
}
