#ifndef Z_OS_POWER_H
#define Z_OS_POWER_H

#include "types.h"

void system_reboot(void) __attribute__((noreturn));
void system_shutdown(void) __attribute__((noreturn));
void qemu_debug_exit(u32 code) __attribute__((noreturn));

#endif
