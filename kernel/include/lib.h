#ifndef Z_OS_LIB_H
#define Z_OS_LIB_H

#include "types.h"

void kprintf(const char *format, ...);
int ksnprintf(char *buffer, usize size, const char *format, ...);
usize kstrlen(const char *text);
void kputs(const char *text);
void kputc(char character);
void panic(const char *message) __attribute__((noreturn));
void halt_forever(void) __attribute__((noreturn));
u64 parse_u64(const char *text, int base, bool *ok);

#endif
