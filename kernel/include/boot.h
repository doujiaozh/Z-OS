#ifndef Z_OS_BOOT_H
#define Z_OS_BOOT_H

#include "types.h"

#define BOOTINFO_MAGIC 0x5A4F5331
#define BOOTINFO_VERSION 1
#define BOOTINFO_MAX_ENTRIES 32

struct e820_entry {
    u64 base;
    u64 length;
    u32 type;
    u32 attributes;
} packed;

struct boot_info {
    u32 magic;
    u32 version;
    u32 entry_count;
    u32 flags;
    struct e820_entry entries[BOOTINFO_MAX_ENTRIES];
} packed;

#define E820_USABLE 1
#define E820_RESERVED 2
#define E820_ACPI_RECLAIM 3
#define E820_ACPI_NVS 4
#define E820_BAD 5

#endif
