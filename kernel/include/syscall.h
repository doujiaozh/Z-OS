#ifndef Z_OS_SYSCALL_H
#define Z_OS_SYSCALL_H

#include "types.h"

struct registers;
void syscall_dispatch(struct registers *registers);

#endif
