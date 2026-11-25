#ifndef Z_OS_ELF_H
#define Z_OS_ELF_H

#include "types.h"

#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'

#define ELF_CLASS_64 2
#define ELF_DATA_LSB 1
#define ELF_VERSION_CURRENT 1
#define ELF_OSABI_SYSV 0
#define ELF_OSABI_LINUX 3

#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_X86_64 62

#define ELF_PROGRAM_LOAD 1
#define ELF_FLAG_EXECUTE 0x1
#define ELF_FLAG_WRITE 0x2
#define ELF_FLAG_READ 0x4

#define ELF_USER_BASE 0x40000000ULL
#define ELF_USER_LIMIT 0x40010000ULL
#define ELF_MAX_LOAD_SEGMENTS 8
#define ELF_MAX_LOAD_PAGES 16

#define ELF_OK 0
#define ELF_ERROR_INVALID -1
#define ELF_ERROR_RANGE -2
#define ELF_ERROR_MEMORY -3
#define ELF_ERROR_OVERLAP -4
#define ELF_ERROR_FLAGS -5

struct elf64_header {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 program_offset;
    u64 section_offset;
    u32 flags;
    u16 header_size;
    u16 program_entry_size;
    u16 program_count;
    u16 section_entry_size;
    u16 section_count;
    u16 section_name_index;
} packed;

struct elf64_program_header {
    u32 type;
    u32 flags;
    u64 offset;
    u64 virtual_address;
    u64 physical_address;
    u64 file_size;
    u64 memory_size;
    u64 alignment;
} packed;

int elf_validate(const void *image, u64 size, u64 *entry, u32 *segment_count);
int elf_load(const void *image, u64 size, u64 *entry);

#endif
