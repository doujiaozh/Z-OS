#include "vmm.h"
#include "io.h"
#include "lib.h"
#include "pmm.h"
#include "spinlock.h"
#include "string.h"

#define PAGE_TABLE_ENTRIES 512
#define ADDRESS_MASK PTE_ADDRESS

static spinlock_t vmm_lock = SPINLOCK_INIT;
static u64 mapped_pages = 0;

static u64 *table_from_entry(u64 entry) {
    return (u64 *)(entry & ADDRESS_MASK);
}

static int allocate_table(u64 **table, u64 flags) {
    u64 frame = pmm_alloc_frame();
    if (frame == 0) {
        return -1;
    }
    *table = (u64 *)frame;
    memset(*table, 0, PAGE_SIZE);
    (void)flags;
    return 0;
}

static int ensure_table(u64 *entry, u64 flags, u64 **table) {
    if ((*entry & PTE_PRESENT) == 0) {
        u64 *new_table = NULL;
        if (allocate_table(&new_table, flags) != 0) {
            return -1;
        }
        *entry = (u64)new_table | PTE_PRESENT | PTE_WRITABLE | (flags & PTE_USER);
        *table = new_table;
        return 0;
    }
    if (flags & PTE_USER) {
        *entry |= PTE_USER;
    }
    *table = table_from_entry(*entry);
    return 0;
}

static int split_huge_page(u64 *pde, u64 virtual_address, u64 flags) {
    u64 old = *pde;
    u64 *page_table = NULL;
    if (allocate_table(&page_table, flags) != 0) {
        return -1;
    }
    u64 base = old & 0x000FFFFFFFE00000ULL;
    u64 page_flags = (old & 0xFFF) & ~PTE_HUGE;
    page_flags &= ~PTE_USER;
    for (u64 index = 0; index < PAGE_TABLE_ENTRIES; index++) {
        page_table[index] = (base + index * PAGE_SIZE) | page_flags | PTE_PRESENT;
    }
    u64 table_flags = PTE_PRESENT | PTE_WRITABLE;
    if (flags & PTE_USER) {
        table_flags |= PTE_USER;
    }
    *pde = (u64)page_table | table_flags;
    tlb_flush(virtual_address & ~(PAGE_SIZE - 1));
    return 0;
}

static u64 *walk_to_pte(u64 virtual_address, u64 flags, bool create) {
    u64 pml4_physical = vmm_pml4_phys();
    u64 *pml4 = (u64 *)pml4_physical;
    u64 pml4_index = (virtual_address >> 39) & 0x1FF;
    u64 pdpt_index = (virtual_address >> 30) & 0x1FF;
    u64 pd_index = (virtual_address >> 21) & 0x1FF;
    u64 pt_index = (virtual_address >> 12) & 0x1FF;

    if (create) {
        u64 *pdpt = NULL;
        if (ensure_table(&pml4[pml4_index], flags, &pdpt) != 0) {
            return NULL;
        }
        u64 *pd = NULL;
        if (ensure_table(&pdpt[pdpt_index], flags, &pd) != 0) {
            return NULL;
        }
        if (pd[pd_index] & PTE_HUGE) {
            if (split_huge_page(&pd[pd_index], virtual_address, flags) != 0) {
                return NULL;
            }
        }
        u64 *pt = NULL;
        if (ensure_table(&pd[pd_index], flags, &pt) != 0) {
            return NULL;
        }
        return &pt[pt_index];
    }

    if ((pml4[pml4_index] & PTE_PRESENT) == 0) {
        return NULL;
    }
    u64 *pdpt = table_from_entry(pml4[pml4_index]);
    if ((pdpt[pdpt_index] & PTE_PRESENT) == 0) {
        return NULL;
    }
    if (pdpt[pdpt_index] & PTE_HUGE) {
        return NULL;
    }
    u64 *pd = table_from_entry(pdpt[pdpt_index]);
    if ((pd[pd_index] & PTE_PRESENT) == 0) {
        return NULL;
    }
    if (pd[pd_index] & PTE_HUGE) {
        return NULL;
    }
    u64 *pt = table_from_entry(pd[pd_index]);
    return &pt[pt_index];
}

void vmm_init(void) {
    mapped_pages = 0;
    u64 pml4 = vmm_pml4_phys();
    if (pml4 == 0 || (pml4 & (PAGE_SIZE - 1)) != 0) {
        panic("invalid page table root");
    }
    kprintf("VMM: PML4 at %p, identity map active below 1 GiB\n", (void *)pml4);
}

u64 vmm_pml4_phys(void) {
    return read_cr3() & ADDRESS_MASK;
}

int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags) {
    if ((virtual_address & (PAGE_SIZE - 1)) != 0 || (physical_address & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    if (virtual_address >= 0x0000800000000000ULL) {
        return -1;
    }
    u64 lock_flags;
    spin_lock_irqsave(&vmm_lock, &lock_flags);
    u64 *pte = walk_to_pte(virtual_address, flags, true);
    if (pte == NULL) {
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        return -1;
    }
    if ((*pte & PTE_PRESENT) == 0) {
        mapped_pages++;
    }
    *pte = (physical_address & ADDRESS_MASK) | (flags & 0xFFF) | PTE_PRESENT;
    tlb_flush(virtual_address);
    spin_unlock_irqrestore(&vmm_lock, lock_flags);
    return 0;
}

int vmm_map_range(u64 virtual_address, u64 physical_address, u64 page_count, u64 flags) {
    for (u64 index = 0; index < page_count; index++) {
        if (vmm_map_page(virtual_address + index * PAGE_SIZE, physical_address + index * PAGE_SIZE, flags) != 0) {
            return -1;
        }
    }
    return 0;
}

int vmm_unmap_page(u64 virtual_address) {
    if ((virtual_address & (PAGE_SIZE - 1)) != 0 || virtual_address >= 0x0000800000000000ULL) {
        return -1;
    }
    u64 lock_flags;
    spin_lock_irqsave(&vmm_lock, &lock_flags);
    u64 *pte = walk_to_pte(virtual_address, 0, false);
    if (pte == NULL || (*pte & PTE_PRESENT) == 0) {
        spin_unlock_irqrestore(&vmm_lock, lock_flags);
        return -1;
    }
    *pte = 0;
    if (mapped_pages > 0) {
        mapped_pages--;
    }
    tlb_flush(virtual_address);
    spin_unlock_irqrestore(&vmm_lock, lock_flags);
    return 0;
}

u64 vmm_get_physical(u64 virtual_address) {
    u64 pml4 = vmm_pml4_phys();
    u64 *pml4_table = (u64 *)pml4;
    u64 pml4_index = (virtual_address >> 39) & 0x1FF;
    if ((pml4_table[pml4_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    u64 *pdpt = table_from_entry(pml4_table[pml4_index]);
    u64 pdpt_index = (virtual_address >> 30) & 0x1FF;
    if ((pdpt[pdpt_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    if (pdpt[pdpt_index] & PTE_HUGE) {
        return (pdpt[pdpt_index] & 0x000FFFFFC0000000ULL) + (virtual_address & 0x3FFFFFFFULL);
    }
    u64 *pd = table_from_entry(pdpt[pdpt_index]);
    u64 pd_index = (virtual_address >> 21) & 0x1FF;
    if ((pd[pd_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    if (pd[pd_index] & PTE_HUGE) {
        return (pd[pd_index] & 0x000FFFFFFFE00000ULL) + (virtual_address & 0x1FFFFFULL);
    }
    u64 *pt = table_from_entry(pd[pd_index]);
    u64 pt_index = (virtual_address >> 12) & 0x1FF;
    if ((pt[pt_index] & PTE_PRESENT) == 0) {
        return 0;
    }
    return (pt[pt_index] & ADDRESS_MASK) + (virtual_address & 0xFFF);
}

bool vmm_is_mapped(u64 virtual_address) {
    return vmm_get_physical(virtual_address) != 0;
}

bool vmm_user_range_valid(u64 address, u64 length) {
    if (length == 0) {
        return true;
    }
    if (address >= 0x0000800000000000ULL || address + length < address) {
        return false;
    }
    u64 first = address & ~(PAGE_SIZE - 1);
    u64 last = (address + length - 1) & ~(PAGE_SIZE - 1);
    for (u64 page = first; page <= last; page += PAGE_SIZE) {
        u64 pml4 = vmm_pml4_phys();
        u64 *pml4_table = (u64 *)pml4;
        u64 pml4_index = (page >> 39) & 0x1FF;
        if ((pml4_table[pml4_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
        u64 *pdpt = table_from_entry(pml4_table[pml4_index]);
        u64 pdpt_index = (page >> 30) & 0x1FF;
        if ((pdpt[pdpt_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
        u64 *pd = table_from_entry(pdpt[pdpt_index]);
        u64 pd_index = (page >> 21) & 0x1FF;
        if ((pd[pd_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
        if (pd[pd_index] & PTE_HUGE) {
            continue;
        }
        u64 *pt = table_from_entry(pd[pd_index]);
        u64 pt_index = (page >> 12) & 0x1FF;
        if ((pt[pt_index] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) {
            return false;
        }
    }
    return true;
}
