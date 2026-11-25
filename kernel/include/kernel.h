#ifndef Z_OS_KERNEL_H
#define Z_OS_KERNEL_H

#include "types.h"

#define Z_OS_NAME "Z-OS"
#define Z_OS_VERSION "0.2.0"
#define Z_OS_CODENAME "ELF"

struct boot_info;

void kmain(struct boot_info *boot);

#endif
