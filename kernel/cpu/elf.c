#include "elf.h"
#include "pmm.h"
#include "string.h"
#include "vmm.h"

static bool elf_ident_valid(const u8 *ident) {
    return ident[0] == ELF_MAGIC_0 && ident[1] == ELF_MAGIC_1 &&
           ident[2] == ELF_MAGIC_2 && ident[3] == ELF_MAGIC_3 &&
           ident[4] == ELF_CLASS_64 && ident[5] == ELF_DATA_LSB &&
           ident[6] == ELF_VERSION_CURRENT &&
           (ident[7] == ELF_OSABI_SYSV || ident[7] == ELF_OSABI_LINUX);
}

int elf_validate(const void *image, u64 size, u64 *entry, u32 *segment_count) {
    if (image == NULL || size < sizeof(struct elf64_header)) {
        return ELF_ERROR_INVALID;
    }
    const u8 *bytes = image;
    struct elf64_header header;
    memcpy(&header, bytes, sizeof(header));
    if (!elf_ident_valid(header.ident)) {
        return ELF_ERROR_INVALID;
    }
    if (header.type != ELF_TYPE_EXEC || header.machine != ELF_MACHINE_X86_64 ||
        header.version != ELF_VERSION_CURRENT) {
        return ELF_ERROR_INVALID;
    }
    if (header.header_size != sizeof(struct elf64_header) ||
        header.program_entry_size != sizeof(struct elf64_program_header)) {
        return ELF_ERROR_INVALID;
    }
    if (header.program_count == 0 || header.program_count > ELF_MAX_LOAD_SEGMENTS) {
        return ELF_ERROR_INVALID;
    }
    u64 program_bytes = (u64)header.program_count * sizeof(struct elf64_program_header);
    if (header.program_offset > size || program_bytes > size - header.program_offset) {
        return ELF_ERROR_INVALID;
    }
    if (header.entry < ELF_USER_BASE || header.entry >= ELF_USER_LIMIT) {
        return ELF_ERROR_RANGE;
    }

    u64 page_bitmap = 0;
    u32 segments = 0;
    bool entry_executable = false;
    for (u32 index = 0; index < header.program_count; index++) {
        struct elf64_program_header program;
        u64 program_address = header.program_offset + (u64)index * sizeof(program);
        memcpy(&program, bytes + program_address, sizeof(program));
        if (program.type != ELF_PROGRAM_LOAD) {
            continue;
        }
        if (program.file_size > program.memory_size) {
            return ELF_ERROR_INVALID;
        }
        if (program.file_size != 0) {
            if (program.offset > size || program.file_size > size - program.offset) {
                return ELF_ERROR_INVALID;
            }
        }
        if (program.memory_size == 0) {
            continue;
        }
        if (program.virtual_address < ELF_USER_BASE ||
            program.virtual_address >= ELF_USER_LIMIT ||
            program.memory_size > ELF_USER_LIMIT - program.virtual_address) {
            return ELF_ERROR_RANGE;
        }
        if ((program.virtual_address & (PAGE_SIZE - 1)) != 0) {
            return ELF_ERROR_RANGE;
        }
        if (program.alignment != 0 && program.alignment != 1 && program.alignment != PAGE_SIZE) {
            return ELF_ERROR_RANGE;
        }
        if ((program.flags & ~0x7U) != 0 || (program.flags & ELF_FLAG_READ) == 0) {
            return ELF_ERROR_FLAGS;
        }
        if ((program.flags & ELF_FLAG_WRITE) != 0 && (program.flags & ELF_FLAG_EXECUTE) != 0) {
            return ELF_ERROR_FLAGS;
        }
        if (header.entry >= program.virtual_address &&
            header.entry < program.virtual_address + program.memory_size &&
            (program.flags & ELF_FLAG_EXECUTE) != 0) {
            entry_executable = true;
        }
        u64 pages = (program.memory_size + PAGE_SIZE - 1) / PAGE_SIZE;
        u64 first_page = (program.virtual_address - ELF_USER_BASE) / PAGE_SIZE;
        if (first_page + pages > ELF_MAX_LOAD_PAGES) {
            return ELF_ERROR_RANGE;
        }
        for (u64 page = 0; page < pages; page++) {
            u64 bit = first_page + page;
            if ((page_bitmap & (1ULL << bit)) != 0) {
                return ELF_ERROR_OVERLAP;
            }
            page_bitmap |= 1ULL << bit;
        }
        segments++;
    }
    if (segments == 0 || !entry_executable) {
        return ELF_ERROR_INVALID;
    }
    if (entry != NULL) {
        *entry = header.entry;
    }
    if (segment_count != NULL) {
        *segment_count = segments;
    }
    return ELF_OK;
}

int elf_load(const void *image, u64 size, u64 *entry) {
    if (entry == NULL) {
        return ELF_ERROR_INVALID;
    }
    u64 image_entry = 0;
    int result = elf_validate(image, size, &image_entry, NULL);
    if (result != ELF_OK) {
        return result;
    }
    const u8 *bytes = image;
    struct elf64_header header;
    memcpy(&header, bytes, sizeof(header));
    u64 mapped_addresses[ELF_MAX_LOAD_PAGES];
    u64 mapped_frames[ELF_MAX_LOAD_PAGES];
    u32 mapped_count = 0;
    for (u32 index = 0; index < header.program_count; index++) {
        struct elf64_program_header program;
        u64 program_address = header.program_offset + (u64)index * sizeof(program);
        memcpy(&program, bytes + program_address, sizeof(program));
        if (program.type != ELF_PROGRAM_LOAD || program.memory_size == 0) {
            continue;
        }
        u64 pages = (program.memory_size + PAGE_SIZE - 1) / PAGE_SIZE;
        for (u64 page = 0; page < pages; page++) {
            u64 frame = pmm_alloc_frame();
            if (frame == 0) {
                result = ELF_ERROR_MEMORY;
                goto rollback;
            }
            u64 segment_offset = page * PAGE_SIZE;
            if (segment_offset < program.file_size) {
                u64 copy_length = program.file_size - segment_offset;
                if (copy_length > PAGE_SIZE) {
                    copy_length = PAGE_SIZE;
                }
                memcpy((void *)frame, bytes + program.offset + segment_offset, (usize)copy_length);
            }
            u64 page_flags = PTE_PRESENT | PTE_USER;
            if ((program.flags & ELF_FLAG_WRITE) != 0) {
                page_flags |= PTE_WRITABLE;
            }
            u64 virtual_address = program.virtual_address + page * PAGE_SIZE;
            if (vmm_map_page(virtual_address, frame, page_flags) != 0) {
                pmm_free_frame(frame);
                result = ELF_ERROR_MEMORY;
                goto rollback;
            }
            mapped_addresses[mapped_count] = virtual_address;
            mapped_frames[mapped_count] = frame;
            mapped_count++;
        }
    }
    *entry = image_entry;
    return ELF_OK;

rollback:
    for (u32 index = mapped_count; index > 0; index--) {
        vmm_unmap_page(mapped_addresses[index - 1]);
        pmm_free_frame(mapped_frames[index - 1]);
    }
    return result;
}
