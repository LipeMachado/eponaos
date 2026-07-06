#ifndef EPONA_ATA_H
#define EPONA_ATA_H

#include <stdint.h>

#define SECTOR_SIZE 512

#define ATA_PRIMARY_DATA     0x1F0
#define ATA_PRIMARY_ERROR    0x1F1
#define ATA_PRIMARY_SECTOR   0x1F2
#define ATA_PRIMARY_LBA_LO   0x1F3
#define ATA_PRIMARY_LBA_MI   0x1F4
#define ATA_PRIMARY_LBA_HI   0x1F5
#define ATA_PRIMARY_DRIVE    0x1F6
#define ATA_PRIMARY_CMD      0x1F7
#define ATA_PRIMARY_STATUS   0x1F7
#define ATA_PRIMARY_CTRL     0x3F6

#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_IDENT  0xEC

#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_SRV  0x10
#define ATA_STATUS_DF   0x20
#define ATA_STATUS_RDY  0x40
#define ATA_STATUS_BSY  0x80

int ata_drive_present(int bus);
int ata_read_sectors(int bus, int master, uint32_t lba, uint8_t count, void *buf);
int ata_write_sectors(int bus, int master, uint32_t lba, uint8_t count, const void *buf);
int ata_identify(int bus, int master, uint16_t *buf);

#endif
