#include "ata.h"
#include "io.h"
#include "serial.h"
#include <stddef.h>

static int ata_wait_busy(uint16_t cmd_port) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(cmd_port) & ATA_STATUS_BSY))
            return 0;
    }
    return -1;
}

static int ata_wait_drq(uint16_t cmd_port) {
    for (int i = 0; i < 100000; i++) {
        uint8_t st = inb(cmd_port);
        if (st & ATA_STATUS_ERR)
            return -1;
        if (st & ATA_STATUS_DRQ)
            return 0;
    }
    return -1;
}

int ata_drive_present(int bus) {
    uint16_t cmd = (bus == 0) ? ATA_PRIMARY_CMD : 0x170 + 7;
    outb(cmd, ATA_STATUS_BSY);
    inb(cmd);
    inb(cmd);
    inb(cmd);
    inb(cmd);
    uint8_t st = inb(cmd);
    return (st != 0xFF);
}

int ata_identify(int bus, int master, uint16_t *buf) {
    uint16_t base = (bus == 0) ? ATA_PRIMARY_DATA : 0x170;
    uint16_t cmd = base + 7;

    if (ata_wait_busy(cmd) < 0)
        return -1;

    outb(base + 6, master ? 0xA0 : 0xB0);
    outb(base + 2, 0);
    outb(base + 3, 0);
    outb(base + 4, 0);
    outb(base + 5, 0);
    outb(cmd, ATA_CMD_IDENT);

    uint8_t st = inb(cmd);
    if (st == 0)
        return -1;

    if (ata_wait_busy(cmd) < 0)
        return -1;

    st = inb(cmd);
    if (st & ATA_STATUS_ERR) {
        inb(base + 1);
        return -1;
    }

    if (ata_wait_drq(cmd) < 0)
        return -1;

    for (int i = 0; i < 256; i++)
        buf[i] = inw(base);

    return 0;
}

int ata_read_sectors(int bus, int master, uint32_t lba, uint8_t count, void *buf) {
    uint16_t base = (bus == 0) ? ATA_PRIMARY_DATA : 0x170;
    uint16_t cmd = base + 7;
    uint16_t *ptr = (uint16_t *) buf;

    if (ata_wait_busy(cmd) < 0)
        return -1;

    outb(base + 6, (master ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F));
    outb(base + 2, count);
    outb(base + 3, (uint8_t) lba);
    outb(base + 4, (uint8_t) (lba >> 8));
    outb(base + 5, (uint8_t) (lba >> 16));
    outb(cmd, ATA_CMD_READ);

    for (int s = 0; s < count; s++) {
        if (ata_wait_busy(cmd) < 0)
            return -1;
        uint8_t st = inb(cmd);
        if (st & ATA_STATUS_ERR)
            return -1;
        if (ata_wait_drq(cmd) < 0)
            return -1;

        for (int i = 0; i < 256; i++)
            ptr[s * 256 + i] = inw(base);
    }

    return 0;
}

int ata_write_sectors(int bus, int master, uint32_t lba, uint8_t count, const void *buf) {
    uint16_t base = (bus == 0) ? ATA_PRIMARY_DATA : 0x170;
    uint16_t cmd = base + 7;
    const uint16_t *ptr = (const uint16_t *) buf;

    if (ata_wait_busy(cmd) < 0)
        return -1;

    outb(base + 6, (master ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F));
    outb(base + 2, count);
    outb(base + 3, (uint8_t) lba);
    outb(base + 4, (uint8_t) (lba >> 8));
    outb(base + 5, (uint8_t) (lba >> 16));
    outb(cmd, ATA_CMD_WRITE);

    for (int s = 0; s < count; s++) {
        if (ata_wait_busy(cmd) < 0)
            return -1;
        uint8_t st = inb(cmd);
        if (st & ATA_STATUS_ERR)
            return -1;
        if (ata_wait_drq(cmd) < 0)
            return -1;

        for (int i = 0; i < 256; i++)
            outw(base, ptr[s * 256 + i]);

        inb(cmd);
    }

    return 0;
}
