#ifndef Z_OS_ATA_H
#define Z_OS_ATA_H

#include "types.h"

#define ATA_SECTOR_SIZE 512
#define ATA_PRIMARY_BASE 0x1F0
#define ATA_PRIMARY_CONTROL 0x3F6
#define ATA_ERROR_NONE 0
#define ATA_ERROR_ABSENT -1
#define ATA_ERROR_TIMEOUT -2
#define ATA_ERROR_IO -3
#define ATA_ERROR_RANGE -4

int ata_init(void);
bool ata_present(void);
u32 ata_sector_count(void);
u32 ata_read_sectors(u32 lba, u32 count, void *buffer);
u32 ata_write_sectors(u32 lba, u32 count, const void *buffer);
int ata_flush(void);
const char *ata_model(void);

#endif
