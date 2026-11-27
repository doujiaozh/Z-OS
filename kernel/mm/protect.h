#ifndef Z_OS_PROTECT_H
#define Z_OS_PROTECT_H

#include "types.h"

void kernel_protect_init(void);
bool kernel_protect_active(void);
u32 kernel_protected_pages(void);

#endif
