#include "zdisk.h"
#include "ata.h"
#include "heap.h"
#include "lib.h"
#include "pmm.h"
#include "string.h"
#include "vmm.h"

#define ZDISK_MAGIC "ZDISK001"
#define ZDISK_CHUNK_SECTORS 128
#define ZDISK_MAX_PAGES 65536

static bool mounted = false;
static u64 part_lba = 0;
static u64 part_sectors = 0;
static u32 file_count = 0;
static struct zdisk_file files[ZDISK_MAX_FILES];

static int parse_partition(u64 lba) {
    u8 header[ATA_SECTOR_SIZE];
    if (ata_read_sectors((u32)lba, 1, header) != 0) {
        return ZDISK_ERROR_RANGE;
    }
    if (memcmp(header, ZDISK_MAGIC, 8) != 0) {
        return ZDISK_ERROR_FORMAT;
    }
    u32 version = 0;
    u32 sector_size = 0;
    u64 sectors = 0;
    u32 count = 0;
    u64 directory_lba = 0;
    memcpy(&version, header + 8, 4);
    memcpy(&sector_size, header + 12, 4);
    memcpy(&sectors, header + 16, 8);
    memcpy(&count, header + 24, 4);
    memcpy(&directory_lba, header + 32, 8);
    if (version != 1 || sector_size != ATA_SECTOR_SIZE || sectors == 0 || count == 0 ||
        count > ZDISK_MAX_FILES) {
        return ZDISK_ERROR_FORMAT;
    }
    u8 table[ATA_SECTOR_SIZE];
    if (ata_read_sectors((u32)(lba + directory_lba), 1, table) != 0) {
        return ZDISK_ERROR_RANGE;
    }
    u32 loaded = 0;
    for (u32 index = 0; index < count; index++) {
        u32 offset = index * 64;
        const char *name = (const char *)(table + offset);
        if (name[0] == 0) {
            continue;
        }
        u64 size = 0;
        u64 first_lba = 0;
        memcpy(&size, table + offset + ZDISK_NAME_MAX, 8);
        memcpy(&first_lba, table + offset + ZDISK_NAME_MAX + 8, 8);
        if (size == 0 || first_lba >= sectors) {
            continue;
        }
        strncpy(files[loaded].name, name, ZDISK_NAME_MAX - 1);
        files[loaded].name[ZDISK_NAME_MAX - 1] = 0;
        files[loaded].size = size;
        files[loaded].first_lba = first_lba;
        loaded++;
    }
    if (loaded == 0) {
        return ZDISK_ERROR_FORMAT;
    }
    part_lba = lba;
    part_sectors = sectors;
    file_count = loaded;
    mounted = true;
    return ZDISK_ERROR_NONE;
}

int zdisk_init(void) {
    mounted = false;
    part_lba = 0;
    part_sectors = 0;
    file_count = 0;
    memset(files, 0, sizeof(files));
    if (!ata_present()) {
        kprintf("ZDISK: no ata device\n");
        return ZDISK_ERROR_ABSENT;
    }
    for (u32 candidate = 0; candidate < 64; candidate++) {
        u64 lba = 2048 + (u64)candidate * 2048;
        if (lba + 2 >= ata_sector_count()) {
            break;
        }
        if (parse_partition(lba) != ZDISK_ERROR_NONE) {
            continue;
        }
        kprintf("ZDISK: mounted at lba %llu, %llu sectors, %u files\n", part_lba, part_sectors, file_count);
        for (u32 index = 0; index < file_count; index++) {
            kprintf("ZDISK: %s %llu bytes at lba %llu\n", files[index].name, files[index].size,
                    files[index].first_lba);
        }
        return ZDISK_ERROR_NONE;
    }
    kprintf("ZDISK: no valid partition found\n");
    return ZDISK_ERROR_FORMAT;
}

bool zdisk_mounted(void) {
    return mounted;
}

u32 zdisk_file_count(void) {
    return file_count;
}

u64 zdisk_part_lba(void) {
    return part_lba;
}

u64 zdisk_part_sectors(void) {
    return part_sectors;
}

const struct zdisk_file *zdisk_lookup(const char *name) {
    if (!mounted || name == NULL) {
        return NULL;
    }
    for (u32 index = 0; index < file_count; index++) {
        if (strcmp(files[index].name, name) == 0) {
            return &files[index];
        }
    }
    return NULL;
}

const struct zdisk_file *zdisk_lookup_index(u32 index) {
    if (!mounted || index >= file_count) {
        return NULL;
    }
    return &files[index];
}

int zdisk_read(const char *name, void *buffer, u64 capacity, u64 *read_bytes) {
    const struct zdisk_file *file = zdisk_lookup(name);
    if (file == NULL) {
        return ZDISK_ERROR_ABSENT;
    }
    if (buffer == NULL || capacity < file->size) {
        return ZDISK_ERROR_RANGE;
    }
    u64 total = (file->size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    u64 done = 0;
    u8 *target = (u8 *)buffer;
    while (done < total) {
        u32 chunk = (u32)(total - done);
        if (chunk > ZDISK_CHUNK_SECTORS) {
            chunk = ZDISK_CHUNK_SECTORS;
        }
        if (ata_read_sectors((u32)(part_lba + file->first_lba + done), chunk, target) != 0) {
            return ZDISK_ERROR_RANGE;
        }
        target += (u64)chunk * ATA_SECTOR_SIZE;
        done += chunk;
    }
    if (read_bytes != NULL) {
        *read_bytes = file->size;
    }
    return ZDISK_ERROR_NONE;
}

int zdisk_load(const char *name, void **buffer, u64 *size) {
    const struct zdisk_file *file = zdisk_lookup(name);
    if (file == NULL) {
        return ZDISK_ERROR_ABSENT;
    }
    if (buffer == NULL || size == NULL || file->size == 0) {
        return ZDISK_ERROR_RANGE;
    }
    u64 total_sectors = (file->size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    u64 bytes_needed = total_sectors * ATA_SECTOR_SIZE;
    u64 pages = (bytes_needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages > ZDISK_MAX_PAGES) {
        return ZDISK_ERROR_RANGE;
    }
    u64 base = pmm_alloc_contiguous((u32)pages);
    if (base == 0) {
        kprintf("ZDISK: contiguous allocation of %llu pages failed\n", pages);
        return ZDISK_ERROR_MEMORY;
    }
    u8 *target = (u8 *)base;
    u64 done = 0;
    while (done < total_sectors) {
        u32 chunk = (u32)(total_sectors - done);
        if (chunk > ZDISK_CHUNK_SECTORS) {
            chunk = ZDISK_CHUNK_SECTORS;
        }
        if (ata_read_sectors((u32)(part_lba + file->first_lba + done), chunk,
                             target + done * ATA_SECTOR_SIZE) != 0) {
            pmm_free_contiguous(base, (u32)pages);
            return ZDISK_ERROR_RANGE;
        }
        done += chunk;
    }
    kprintf("ZDISK: loaded %llu sectors at physical %llu, %llu pages\n", total_sectors, base, pages);
    *buffer = target;
    *size = file->size;
    return ZDISK_ERROR_NONE;
}

const char *zdisk_error_text(int error) {
    switch (error) {
        case ZDISK_ERROR_NONE:
            return "ok";
        case ZDISK_ERROR_ABSENT:
            return "missing";
        case ZDISK_ERROR_FORMAT:
            return "bad format";
        case ZDISK_ERROR_RANGE:
            return "out of range";
        case ZDISK_ERROR_MEMORY:
            return "out of memory";
        default:
            return "unknown";
    }
}
