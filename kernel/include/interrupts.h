#ifndef Z_OS_INTERRUPTS_H
#define Z_OS_INTERRUPTS_H

#include "types.h"

struct registers {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rdi;
    u64 rsi;
    u64 rbp;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;
    u64 vector;
    u64 error;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

void interrupts_init(void);
void exceptions_init(void);
void interrupt_dispatch(struct registers *registers);
const char *exception_name(u64 vector);

#endif
