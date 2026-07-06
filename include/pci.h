#ifndef EPONA_PCI_H
#define EPONA_PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_VENDOR_NONE 0xFFFF

/* BAR types */
#define PCI_BAR_IO     0x01
#define PCI_BAR_MEM64  0x04

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t  revision_id;
    uint8_t  prog_if;
    uint8_t  subclass;
    uint8_t  class_code;
    uint8_t  cache_line_size;
    uint8_t  latency_timer;
    uint8_t  header_type;
    uint8_t  bist;
    uint32_t bar[6];
    uint32_t cardbus_cis;
    uint16_t subsystem_vendor;
    uint16_t subsystem_id;
    uint32_t expansion_rom;
    uint8_t  capabilities;
    uint8_t  reserved[7];
    uint8_t  interrupt_line;
    uint8_t  interrupt_pin;
    uint8_t  min_grant;
    uint8_t  max_latency;
} __attribute__((packed)) pci_header_t;

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t func;
    uint16_t vendor;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    pci_header_t config;
} pci_device_t;

uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
void pci_enumerate(void);
void pci_print_device(const pci_device_t *dev);

#endif
