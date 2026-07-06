#include "pci.h"
#include "io.h"
#include "serial.h"
#include <stddef.h>

void pci_check_bus(uint8_t bus);

#define MAX_DEVICES 256
static pci_device_t g_pci_devices[MAX_DEVICES];
static int g_pci_count = 0;

static uint32_t pci_make_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return ((uint32_t) 0x80000000) |
           ((uint32_t) bus << 16) |
           ((uint32_t) dev << 11) |
           ((uint32_t) func << 8) |
           (offset & 0xFC);
}

uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, pci_make_addr(bus, dev, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDR, pci_make_addr(bus, dev, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

static void pci_read_header(uint8_t bus, uint8_t dev, uint8_t func, pci_header_t *h) {
    uint32_t *hw = (uint32_t *) h;
    for (size_t i = 0; i < sizeof(pci_header_t) / 4; i++)
        hw[i] = pci_read_config(bus, dev, func, (uint8_t) (i * 4));
}

static void pci_check_func(uint8_t bus, uint8_t dev, uint8_t func) {
    pci_header_t h;
    pci_read_header(bus, dev, func, &h);
    if (h.vendor_id == PCI_VENDOR_NONE)
        return;

    if (g_pci_count >= MAX_DEVICES)
        return;

    pci_device_t *d = &g_pci_devices[g_pci_count++];
    d->bus = bus;
    d->device = dev;
    d->func = func;
    d->vendor = h.vendor_id;
    d->device_id = h.device_id;
    d->class_code = h.class_code;
    d->subclass = h.subclass;
    d->prog_if = h.prog_if;
    d->config = h;

    if (h.class_code == 0x06 && h.subclass == 0x04) {
        uint32_t bus_reg = pci_read_config(bus, dev, func, 0x44);
        uint8_t secondary_bus = (bus_reg >> 8) & 0xFF;
        if (secondary_bus != bus)
            pci_check_bus(secondary_bus);
    }
}

static void pci_check_device(uint8_t bus, uint8_t dev) {
    pci_header_t h;
    pci_read_header(bus, dev, 0, &h);
    if (h.vendor_id == PCI_VENDOR_NONE)
        return;

    pci_check_func(bus, dev, 0);
    if (h.header_type & 0x80) {
        for (int func = 1; func < 8; func++)
            pci_check_func(bus, dev, func);
    }
}

void pci_check_bus(uint8_t bus) {
    for (int dev = 0; dev < 32; dev++)
        pci_check_device(bus, dev);
}

void pci_enumerate(void) {
    serial_print("[pci] enumerating...\n");
    g_pci_count = 0;

    pci_header_t h;
    pci_read_header(0, 0, 0, &h);
    if (h.header_type & 0x80) {
        for (int func = 0; func < 8; func++) {
            uint32_t vendor = pci_read_config(0, 0, func, 0) & 0xFFFF;
            if (vendor != PCI_VENDOR_NONE)
                pci_check_bus(func);
        }
    } else {
        pci_check_bus(0);
    }

    serial_print("[pci] done: ");
    serial_print_dec(g_pci_count);
    serial_print(" devices found.\n");

    for (int i = 0; i < g_pci_count; i++)
        pci_print_device(&g_pci_devices[i]);
}

void pci_print_device(const pci_device_t *d) {
    serial_print("  ");
    serial_print_hex(d->bus); serial_print(":");
    serial_print_hex(d->device); serial_print(":");
    serial_print_hex(d->func); serial_print("  ");
    serial_print_hex(d->vendor); serial_print(":");
    serial_print_hex(d->device_id); serial_print("  ");
    serial_print_hex(d->class_code); serial_print(".");
    serial_print_hex(d->subclass); serial_print(".");
    serial_print_hex(d->prog_if); serial_print("\n");
}
