#ifndef Z_OS_VMM_H
#define Z_OS_VMM_H

#include "types.h"

#define PTE_PRESENT 0x001ULL
#define PTE_WRITABLE 0x002ULL
#define PTE_USER 0x004ULL
#define PTE_WRITE_THROUGH 0x008ULL
#define PTE_CACHE_DISABLE 0x010ULL
#define PTE_ACCESSED 0x020ULL
#define PTE_DIRTY 0x040ULL
#define PTE_HUGE 0x080ULL
#define PTE_GLOBAL 0x100ULL
#define PTE_ADDRESS 0x000FFFFFFFFFF000ULL

void vmm_init(void);
u64 vmm_pml4_phys(void);
int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags);
int vmm_map_range(u64 virtual_address, u64 physical_address, u64 page_count, u64 flags);
int vmm_unmap_page(u64 virtual_address);
u64 vmm_get_physical(u64 virtual_address);
bool vmm_is_mapped(u64 virtual_address);
bool vmm_user_range_valid(u64 address, u64 length);

#endif
