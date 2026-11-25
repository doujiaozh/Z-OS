#include "zfs.h"
#include "ata.h"
#include "heap.h"
#include "lib.h"
#include "string.h"
#include "vfs.h"

struct zfs_entry {
    char name[ZFS_NAME_MAX];
    u32 size;
    u32 first_sector;
    u32 used;
    u32 reserved;
};

struct zfs_superblock {
    u32 magic;
    u32 version;
    u32 block_size;
    u32 total_sectors;
    u32 data_start;
    u32 max_files;
    u32 used_files;
    u32 reserved[9];
};

static struct zfs_superblock superblock;
static struct zfs_entry table[ZFS_MAX_FILES];
static bool mounted = false;

static struct zfs_entry *find_entry(const char *name) {
    for (u32 index = 0; index < ZFS_MAX_FILES; index++) {
        if (table[index].used && strcmp(table[index].name, name) == 0) {
            return &table[index];
        }
    }
    return NULL;
}

static struct zfs_entry *find_free_entry(void) {
    for (u32 index = 0; index < ZFS_MAX_FILES; index++) {
        if (!table[index].used) {
            return &table[index];
        }
    }
    return NULL;
}

static u32 count_used(void) {
    u32 count = 0;
    for (u32 index = 0; index < ZFS_MAX_FILES; index++) {
        if (table[index].used) {
            count++;
        }
    }
    return count;
}

static int write_metadata(void) {
    u8 super_buffer[ZFS_BLOCK_SIZE];
    u8 table_buffer[ZFS_TABLE_SECTORS * ZFS_BLOCK_SIZE];
    memset(super_buffer, 0, sizeof(super_buffer));
    superblock.used_files = count_used();
    memcpy(super_buffer, &superblock, sizeof(superblock));
    if (ata_write_sectors(ZFS_SUPER_SECTOR, 1, super_buffer) != 0) {
        return ZFS_ERROR_RANGE;
    }
    memset(table_buffer, 0, sizeof(table_buffer));
    memcpy(table_buffer, table, sizeof(table));
    if (ata_write_sectors(ZFS_TABLE_SECTOR, ZFS_TABLE_SECTORS, table_buffer) != 0) {
        return ZFS_ERROR_RANGE;
    }
    return ZFS_ERROR_NONE;
}

int zfs_format(void) {
    if (!ata_present()) {
        return ZFS_ERROR_ABSENT;
    }
    if (ata_sector_count() < ZFS_DATA_START + ZFS_MAX_FILES * ZFS_SECTORS_PER_FILE) {
        return ZFS_ERROR_RANGE;
    }
    memset(&superblock, 0, sizeof(superblock));
    superblock.magic = ZFS_MAGIC;
    superblock.version = ZFS_VERSION;
    superblock.block_size = ZFS_BLOCK_SIZE;
    superblock.total_sectors = ata_sector_count();
    superblock.data_start = ZFS_DATA_START;
    superblock.max_files = ZFS_MAX_FILES;
    memset(table, 0, sizeof(table));
    int result = write_metadata();
    if (result != ZFS_ERROR_NONE) {
        return result;
    }
    mounted = true;
    return ZFS_ERROR_NONE;
}

int zfs_mount(void) {
    if (!ata_present()) {
        return ZFS_ERROR_ABSENT;
    }
    u8 super_buffer[ZFS_BLOCK_SIZE];
    u8 table_buffer[ZFS_TABLE_SECTORS * ZFS_BLOCK_SIZE];
    if (ata_read_sectors(ZFS_SUPER_SECTOR, 1, super_buffer) != 0) {
        return ZFS_ERROR_RANGE;
    }
    memset(&superblock, 0, sizeof(superblock));
    memcpy(&superblock, super_buffer, sizeof(superblock));
    if (superblock.magic != ZFS_MAGIC || superblock.version != ZFS_VERSION ||
        superblock.block_size != ZFS_BLOCK_SIZE) {
        mounted = false;
        return ZFS_ERROR_FORMAT;
    }
    memset(table_buffer, 0, sizeof(table_buffer));
    if (ata_read_sectors(ZFS_TABLE_SECTOR, ZFS_TABLE_SECTORS, table_buffer) != 0) {
        return ZFS_ERROR_RANGE;
    }
    memset(table, 0, sizeof(table));
    memcpy(table, table_buffer, sizeof(table));
    mounted = true;
    return ZFS_ERROR_NONE;
}

bool zfs_mounted(void) {
    return mounted;
}

u32 zfs_disk_sectors(void) {
    return ata_sector_count();
}

u32 zfs_file_count(void) {
    return count_used();
}

u32 zfs_list(struct zfs_dirent *buffer, u32 capacity) {
    if (!mounted || buffer == NULL) {
        return 0;
    }
    u32 written = 0;
    for (u32 index = 0; index < ZFS_MAX_FILES && written < capacity; index++) {
        if (!table[index].used) {
            continue;
        }
        strncpy(buffer[written].name, table[index].name, ZFS_NAME_MAX - 1);
        buffer[written].name[ZFS_NAME_MAX - 1] = 0;
        buffer[written].size = table[index].size;
        buffer[written].first_sector = table[index].first_sector;
        written++;
    }
    return written;
}

int zfs_write_file(const char *name, const void *data, u32 size) {
    if (!mounted || name == NULL || data == NULL) {
        return ZFS_ERROR_ABSENT;
    }
    if (size > ZFS_MAX_FILE_SIZE || strlen(name) >= ZFS_NAME_MAX) {
        return ZFS_ERROR_RANGE;
    }
    struct zfs_entry *entry = find_entry(name);
    u32 slot = 0;
    if (entry == NULL) {
        entry = find_free_entry();
        if (entry == NULL) {
            return ZFS_ERROR_NO_SPACE;
        }
        slot = (u32)(entry - table);
        memset(entry, 0, sizeof(struct zfs_entry));
        strncpy(entry->name, name, ZFS_NAME_MAX - 1);
        entry->first_sector = ZFS_DATA_START + slot * ZFS_SECTORS_PER_FILE;
        entry->used = 1;
    }
    u32 sectors = (size + ZFS_BLOCK_SIZE - 1) / ZFS_BLOCK_SIZE;
    if (sectors == 0) {
        sectors = 1;
    }
    u8 *buffer = kzalloc(sectors * ZFS_BLOCK_SIZE);
    if (buffer == NULL) {
        return ZFS_ERROR_NO_SPACE;
    }
    if (size > 0) {
        memcpy(buffer, data, size);
    }
    int result = ata_write_sectors(entry->first_sector, sectors, buffer);
    kfree(buffer);
    if (result != 0) {
        if (entry->size == 0) {
            memset(entry, 0, sizeof(struct zfs_entry));
        }
        return ZFS_ERROR_RANGE;
    }
    entry->size = size;
    return write_metadata();
}

int zfs_read_file(const char *name, void *buffer, u32 capacity) {
    if (!mounted || name == NULL || buffer == NULL) {
        return ZFS_ERROR_ABSENT;
    }
    struct zfs_entry *entry = find_entry(name);
    if (entry == NULL) {
        return ZFS_ERROR_NOT_FOUND;
    }
    if (entry->size > capacity) {
        return ZFS_ERROR_RANGE;
    }
    u32 sectors = (entry->size + ZFS_BLOCK_SIZE - 1) / ZFS_BLOCK_SIZE;
    if (sectors == 0) {
        return 0;
    }
    u8 *scratch = kmalloc(sectors * ZFS_BLOCK_SIZE);
    if (scratch == NULL) {
        return ZFS_ERROR_NO_SPACE;
    }
    int result = ata_read_sectors(entry->first_sector, sectors, scratch);
    if (result != 0) {
        kfree(scratch);
        return ZFS_ERROR_RANGE;
    }
    memcpy(buffer, scratch, entry->size);
    kfree(scratch);
    return (int)entry->size;
}

int zfs_remove_file(const char *name) {
    if (!mounted || name == NULL) {
        return ZFS_ERROR_ABSENT;
    }
    struct zfs_entry *entry = find_entry(name);
    if (entry == NULL) {
        return ZFS_ERROR_NOT_FOUND;
    }
    memset(entry, 0, sizeof(struct zfs_entry));
    return write_metadata();
}

int zfs_sync(void) {
    if (!mounted) {
        return ZFS_ERROR_ABSENT;
    }
    int result = write_metadata();
    if (result != ZFS_ERROR_NONE) {
        return result;
    }
    return (int)ata_flush();
}

static struct vfs_node *disk_directory(void) {
    struct vfs_node *root = vfs_root();
    struct vfs_node *node = vfs_lookup(root, "/disk");
    return node;
}

int zfs_mount_vfs(void) {
    if (!mounted) {
        return ZFS_ERROR_ABSENT;
    }
    struct vfs_node *root = vfs_root();
    struct vfs_node *directory = vfs_lookup(root, "/disk");
    if (directory == NULL) {
        int created = vfs_mkdir(root, "/disk");
        if (created != VFS_OK && created != VFS_ERROR_EXISTS) {
            return ZFS_ERROR_NO_SPACE;
        }
        directory = vfs_lookup(root, "/disk");
    }
    if (directory == NULL || !vfs_is_directory(directory)) {
        return ZFS_ERROR_FORMAT;
    }
    struct zfs_dirent entries[ZFS_MAX_FILES];
    u32 count = zfs_list(entries, ZFS_MAX_FILES);
    u8 *buffer = kmalloc(ZFS_MAX_FILE_SIZE);
    if (buffer == NULL) {
        return ZFS_ERROR_NO_SPACE;
    }
    for (u32 index = 0; index < count; index++) {
        char path[VFS_PATH_MAX];
        ksnprintf(path, sizeof(path), "/disk/%s", entries[index].name);
        struct vfs_node *node = vfs_lookup(root, path);
        if (node == NULL) {
            int created = vfs_create(directory, entries[index].name);
            if (created != VFS_OK && created != VFS_ERROR_EXISTS) {
                continue;
            }
            node = vfs_lookup(root, path);
        }
        if (node == NULL || !vfs_is_file(node)) {
            continue;
        }
        int read = zfs_read_file(entries[index].name, buffer, ZFS_MAX_FILE_SIZE);
        if (read < 0) {
            continue;
        }
        vfs_truncate(node);
        if (read > 0) {
            vfs_write(node, 0, buffer, (u64)read);
        }
    }
    kfree(buffer);
    return ZFS_ERROR_NONE;
}

int zfs_sync_vfs(void) {
    if (!mounted) {
        return ZFS_ERROR_ABSENT;
    }
    struct vfs_node *directory = disk_directory();
    if (directory == NULL) {
        return ZFS_ERROR_FORMAT;
    }
    struct vfs_dirent entries[64];
    u32 count = vfs_list(directory, entries, 64);
    u8 *buffer = kmalloc(ZFS_MAX_FILE_SIZE);
    if (buffer == NULL) {
        return ZFS_ERROR_NO_SPACE;
    }
    for (u32 index = 0; index < count; index++) {
        if (entries[index].type != VFS_TYPE_FILE) {
            continue;
        }
        char path[VFS_PATH_MAX];
        ksnprintf(path, sizeof(path), "/disk/%s", entries[index].name);
        struct vfs_node *node = vfs_lookup(vfs_root(), path);
        if (node == NULL) {
            continue;
        }
        struct vfs_stat stat;
        if (vfs_stat(node, &stat) != VFS_OK) {
            continue;
        }
        u64 size = stat.size > ZFS_MAX_FILE_SIZE ? ZFS_MAX_FILE_SIZE : stat.size;
        memset(buffer, 0, ZFS_MAX_FILE_SIZE);
        int read = vfs_read(node, 0, buffer, size);
        if (read < 0) {
            continue;
        }
        zfs_write_file(entries[index].name, buffer, (u32)read);
    }
    struct zfs_dirent disk_entries[ZFS_MAX_FILES];
    u32 disk_count = zfs_list(disk_entries, ZFS_MAX_FILES);
    for (u32 index = 0; index < disk_count; index++) {
        bool present = false;
        for (u32 local = 0; local < count; local++) {
            if (strcmp(disk_entries[index].name, entries[local].name) == 0) {
                present = true;
                break;
            }
        }
        if (!present) {
            zfs_remove_file(disk_entries[index].name);
        }
    }
    kfree(buffer);
    return zfs_sync();
}
