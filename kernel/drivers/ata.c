#include "ata.h"
#include "io.h"
#include "lib.h"
#include "string.h"

#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_FEATURES 1
#define ATA_REG_SECTOR_COUNT 2
#define ATA_REG_LBA_LOW 3
#define ATA_REG_LBA_MID 4
#define ATA_REG_LBA_HIGH 5
#define ATA_REG_DRIVE 6
#define ATA_REG_STATUS 7
#define ATA_REG_COMMAND 7

#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_BSY 0x80

#define ATA_COMMAND_READ 0x20
#define ATA_COMMAND_WRITE 0x30
#define ATA_COMMAND_IDENTIFY 0xEC
#define ATA_COMMAND_FLUSH 0xE7

#define ATA_TIMEOUT 2000000

static bool device_present = false;
static u32 device_sectors = 0;
static char device_model[41];

static int ata_wait_ready(void) {
    for (u32 spin = 0; spin < ATA_TIMEOUT; spin++) {
        u8 status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
        if ((status & ATA_STATUS_BSY) == 0) {
            if ((status & ATA_STATUS_ERR) != 0 || (status & ATA_STATUS_DF) != 0) {
                return ATA_ERROR_IO;
            }
            return ATA_ERROR_NONE;
        }
    }
    return ATA_ERROR_TIMEOUT;
}

static int ata_wait_drq(void) {
    for (u32 spin = 0; spin < ATA_TIMEOUT; spin++) {
        u8 status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
        if ((status & ATA_STATUS_BSY) == 0 && (status & ATA_STATUS_DRQ) != 0) {
            return ATA_ERROR_NONE;
        }
        if ((status & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0) {
            return ATA_ERROR_IO;
        }
    }
    return ATA_ERROR_TIMEOUT;
}

static void ata_select_lba(u32 lba) {
    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_PRIMARY_BASE + ATA_REG_SECTOR_COUNT, 1);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LOW, (u8)(lba & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID, (u8)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HIGH, (u8)((lba >> 16) & 0xFF));
}

int ata_init(void) {
    device_present = false;
    device_sectors = 0;
    memset(device_model, 0, sizeof(device_model));
    outb(ATA_PRIMARY_CONTROL, 0x00);
    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE, 0xA0);
    for (u32 spin = 0; spin < 10000; spin++) {
        if ((inb(ATA_PRIMARY_BASE + ATA_REG_STATUS) & ATA_STATUS_BSY) == 0) {
            break;
        }
    }
    outb(ATA_PRIMARY_BASE + ATA_REG_SECTOR_COUNT, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LOW, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HIGH, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_COMMAND, ATA_COMMAND_IDENTIFY);
    u8 status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
    if (status == 0) {
        return ATA_ERROR_ABSENT;
    }
    if (ata_wait_ready() != ATA_ERROR_NONE) {
        return ATA_ERROR_ABSENT;
    }
    if (inb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID) != 0 || inb(ATA_PRIMARY_BASE + ATA_REG_LBA_HIGH) != 0) {
        return ATA_ERROR_ABSENT;
    }
    if (ata_wait_drq() != ATA_ERROR_NONE) {
        return ATA_ERROR_ABSENT;
    }
    u16 identify[256];
    for (u32 index = 0; index < 256; index++) {
        identify[index] = inw(ATA_PRIMARY_BASE + ATA_REG_DATA);
    }
    u32 sectors = ((u32)identify[61] << 16) | identify[60];
    if (sectors == 0) {
        sectors = ((u32)identify[61] << 16) | identify[60];
    }
    if (sectors == 0) {
        return ATA_ERROR_ABSENT;
    }
    for (u32 index = 0; index < 20; index++) {
        u16 value = identify[27 + index];
        device_model[index * 2] = (char)(value >> 8);
        device_model[index * 2 + 1] = (char)(value & 0xFF);
    }
    device_model[40] = 0;
    for (u32 index = 39; index > 0; index--) {
        if (device_model[index] == ' ') {
            device_model[index] = 0;
        } else {
            break;
        }
    }
    device_sectors = sectors;
    device_present = true;
    return ATA_ERROR_NONE;
}

bool ata_present(void) {
    return device_present;
}

u32 ata_sector_count(void) {
    return device_sectors;
}

const char *ata_model(void) {
    return device_model;
}

u32 ata_read_sectors(u32 lba, u32 count, void *buffer) {
    if (!device_present) {
        return ATA_ERROR_ABSENT;
    }
    if (buffer == NULL || count == 0 || lba + count > device_sectors) {
        return ATA_ERROR_RANGE;
    }
    u16 *target = (u16 *)buffer;
    for (u32 sector = 0; sector < count; sector++) {
        ata_select_lba(lba + sector);
        outb(ATA_PRIMARY_BASE + ATA_REG_COMMAND, ATA_COMMAND_READ);
        int result = ata_wait_drq();
        if (result != ATA_ERROR_NONE) {
            return (u32)result;
        }
        for (u32 word = 0; word < 256; word++) {
            target[sector * 256 + word] = inw(ATA_PRIMARY_BASE + ATA_REG_DATA);
        }
        if (ata_wait_ready() != ATA_ERROR_NONE) {
            return ATA_ERROR_IO;
        }
    }
    return 0;
}

u32 ata_write_sectors(u32 lba, u32 count, const void *buffer) {
    if (!device_present) {
        return ATA_ERROR_ABSENT;
    }
    if (buffer == NULL || count == 0 || lba + count > device_sectors) {
        return ATA_ERROR_RANGE;
    }
    const u16 *source = (const u16 *)buffer;
    for (u32 sector = 0; sector < count; sector++) {
        ata_select_lba(lba + sector);
        outb(ATA_PRIMARY_BASE + ATA_REG_COMMAND, ATA_COMMAND_WRITE);
        int result = ata_wait_drq();
        if (result != ATA_ERROR_NONE) {
            return (u32)result;
        }
        for (u32 word = 0; word < 256; word++) {
            outw(ATA_PRIMARY_BASE + ATA_REG_DATA, source[sector * 256 + word]);
        }
        if (ata_wait_ready() != ATA_ERROR_NONE) {
            return ATA_ERROR_IO;
        }
    }
    return (u32)ata_flush();
}

int ata_flush(void) {
    if (!device_present) {
        return ATA_ERROR_ABSENT;
    }
    int ready = ata_wait_ready();
    if (ready != ATA_ERROR_NONE) {
        return ready;
    }
    outb(ATA_PRIMARY_BASE + ATA_REG_COMMAND, ATA_COMMAND_FLUSH);
    return ata_wait_ready();
}
