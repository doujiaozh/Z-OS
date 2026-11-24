#ifndef Z_OS_TYPES_H
#define Z_OS_TYPES_H

typedef unsigned char u8;
typedef signed char i8;
typedef unsigned short u16;
typedef signed short i16;
typedef unsigned int u32;
typedef signed int i32;
typedef unsigned long long u64;
typedef signed long long i64;
typedef __SIZE_TYPE__ usize;
typedef __INTPTR_TYPE__ isize;
typedef _Bool bool;

#define true 1
#define false 0
#define NULL ((void *)0)
#define offsetof(type, member) __builtin_offsetof(type, member)
#define z_noreturn __attribute__((noreturn))
#define packed __attribute__((packed))
#define aligned(n) __attribute__((aligned(n)))
#define unused __attribute__((unused))

#define PAGE_SIZE 4096ULL

#endif
