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

static void nx_page_table_level(u64 *pdpt, u32 pdpt_index);
static void nx_identity_memory(u64 kernel_start, u64 kernel_end);

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
            if (page_address < kernel_start) {
                flags |= PTE_NO_EXECUTE;
            }
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
    nx_identity_memory(kernel_start, kernel_limit);
    write_cr3(vmm_pml4_phys());
    protect_active = true;
}

static void nx_page_table_level(u64 *pdpt, u32 pdpt_index) {
    u64 entry = pdpt[pdpt_index];
    if ((entry & PTE_PRESENT) == 0) {
        return;
    }
    if ((entry & PTE_HUGE) != 0) {
        pdpt[pdpt_index] = entry | PTE_NO_EXECUTE;
        return;
    }
    u64 *pd = (u64 *)(entry & PTE_ADDRESS);
    for (u32 pd_index = 0; pd_index < 512; pd_index++) {
        u64 pd_entry = pd[pd_index];
        if ((pd_entry & PTE_PRESENT) == 0) {
            continue;
        }
        if ((pd_entry & PTE_HUGE) != 0) {
            pd[pd_index] = pd_entry | PTE_NO_EXECUTE;
            continue;
        }
        u64 *pt = (u64 *)(pd_entry & PTE_ADDRESS);
        for (u32 pt_index = 0; pt_index < 512; pt_index++) {
            u64 pte = pt[pt_index];
            if ((pte & PTE_PRESENT) == 0) {
                continue;
            }
            pt[pt_index] = pte | PTE_NO_EXECUTE;
        }
    }
}

static void nx_identity_memory(u64 kernel_start, u64 kernel_end) {
    u64 *pml4 = (u64 *)vmm_pml4_phys();
    if ((pml4[0] & PTE_PRESENT) == 0 || (pml4[0] & PTE_HUGE) != 0) {
        return;
    }
    u64 *pdpt = (u64 *)(pml4[0] & PTE_ADDRESS);
    if ((pdpt[0] & PTE_PRESENT) == 0) {
        return;
    }
    if ((pdpt[0] & PTE_HUGE) != 0) {
        pdpt[0] |= PTE_NO_EXECUTE;
        return;
    }
    u64 *pd = (u64 *)(pdpt[0] & PTE_ADDRESS);
    for (u32 pd_index = 0; pd_index < 512; pd_index++) {
        u64 entry = pd[pd_index];
        if ((entry & PTE_PRESENT) == 0 || (entry & PTE_HUGE) == 0) {
            continue;
        }
        u64 base = entry & PTE_ADDRESS;
        u64 limit = base + 0x200000ULL;
        if (limit <= kernel_start || base >= kernel_end) {
            pd[pd_index] = entry | PTE_NO_EXECUTE;
        }
    }
    for (u32 pml4_index = 510; pml4_index < 512; pml4_index++) {
        u64 entry = pml4[pml4_index];
        if ((entry & PTE_PRESENT) == 0 || (entry & PTE_HUGE) != 0) {
            continue;
        }
        u64 *pdpt = (u64 *)(entry & PTE_ADDRESS);
        for (u32 pdpt_index = 0; pdpt_index < 512; pdpt_index++) {
            nx_page_table_level(pdpt, pdpt_index);
        }
    }
}

bool kernel_protect_active(void) {
    return protect_active;
}

u32 kernel_protected_pages(void) {
    return protected_pages;
}
