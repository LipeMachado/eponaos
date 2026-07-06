#include "rtl8139.h"
#include "io.h"
#include "pci.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"
#include "net.h"
#include <stddef.h>

static uint16_t g_io_base = 0;
static uint8_t g_mac[ETH_ALEN];
static uint8_t *g_rx_buffer = NULL;
static uint8_t *g_tx_buffer[RTL_TX_BUFS];
static int g_tx_cur = 0;
static uint32_t g_rx_offset = 0;

static const char g_hex[] = "0123456789ABCDEF";

static void print_hex_byte(uint8_t v) {
    serial_putc(g_hex[(v >> 4) & 0x0F]);
    serial_putc(g_hex[v & 0x0F]);
}

#define reg8(r)  ((uint16_t) (g_io_base + (r)))
#define reg16(r) ((uint16_t) (g_io_base + (r)))
#define reg32(r) ((uint16_t) (g_io_base + (r)))

int rtl8139_init(void) {
    serial_print("[rtl8139] probing PCI...\n");

    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t vid = pci_read_config((uint8_t) bus, (uint8_t) dev, 0, 0);
            if ((vid & 0xFFFF) == RTL8139_VENDOR && ((vid >> 16) & 0xFFFF) == RTL8139_DEVICE) {
                serial_print("[rtl8139] found at ");
                serial_print_hex(bus);
                serial_print(":");
                serial_print_hex(dev);
                serial_print("\n");

                uint32_t bar0 = pci_read_config((uint8_t) bus, (uint8_t) dev, 0, 0x10);
                g_io_base = (uint16_t) (bar0 & 0xFFFC);

                pci_write_config((uint8_t) bus, (uint8_t) dev, 0, 0x04, 0x05);

                serial_print("[rtl8139] I/O base: ");
                serial_print_hex(g_io_base);
                serial_print("\n");
                goto found;
            }
        }
    }

    serial_print("[rtl8139] not found\n");
    return -1;

found:
    serial_print("[rtl8139] resetting...\n");
    outb(reg8(RTL_CR), RTL_CR_RST);
    for (int i = 0; i < 10000; i++) {
        if (!(inb(reg8(RTL_CR)) & RTL_CR_RST))
            break;
    }

    for (int i = 0; i < ETH_ALEN; i++)
        g_mac[i] = inb(reg8(RTL_MAC + i));

    serial_print("[rtl8139] MAC: ");
    for (int i = 0; i < ETH_ALEN; i++) {
        print_hex_byte(g_mac[i]);
        if (i < 5) serial_print(":");
    }
    serial_print("\n");

    g_rx_buffer = (uint8_t *) pmm_alloc_contiguous(3);
    if (!g_rx_buffer) {
        serial_print("[rtl8139] RX buffer alloc failed\n");
        return -1;
    }
    memset(g_rx_buffer, 0, 3 * 4096);
    g_rx_offset = 0;

    outl(reg32(RTL_RBSTART), (uint32_t) (uintptr_t) g_rx_buffer);

    for (int i = 0; i < RTL_TX_BUFS; i++) {
        g_tx_buffer[i] = (uint8_t *) pmm_alloc();
        if (!g_tx_buffer[i]) {
            serial_print("[rtl8139] TX buffer alloc failed\n");
            return -1;
        }
    }

    outb(reg8(RTL_IMR), 0x00);

    outl(reg32(RTL_RCR), RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB | RTL_RCR_WRAP);

    outb(reg8(RTL_CR), RTL_CR_TE | RTL_CR_RE);

    serial_print("[rtl8139] ready\n");
    return 0;
}

int rtl8139_send(const void *data, int len) {
    if (len < 14 || len > 1792)
        return -1;

    uint8_t *buf = g_tx_buffer[g_tx_cur];
    for (int i = 0; i < len; i++)
        buf[i] = ((const uint8_t *) data)[i];

    outl(reg32(RTL_TSAD0 + g_tx_cur * 4), (uint32_t) (uintptr_t) buf);

    uint32_t cmd = (uint32_t) (len & 0x1FFF) | 0x10000;
    outl(reg32(RTL_TSD0 + g_tx_cur * 4), cmd);

    g_tx_cur = (g_tx_cur + 1) % RTL_TX_BUFS;
    return 0;
}

int rtl8139_poll(void) {
    uint16_t status = inw(reg16(RTL_ISR));
    if (status & RTL_ISR_ROK) {
        outw(reg16(RTL_ISR), RTL_ISR_ROK);

        int processed = 0;
        for (int spin = 0; spin < 1000; spin++) {
            uint16_t rx_status = *(volatile uint16_t *) (g_rx_buffer + g_rx_offset);
            uint16_t rx_len = *(volatile uint16_t *) (g_rx_buffer + g_rx_offset + 2);

            if (!(rx_status & (0x8000 | 0x0001)))
                continue;

            rx_len &= 0x3FFF;
            if (rx_len == 0 || rx_len == 0xFFF0)
                break;

            uint8_t *pkt = g_rx_buffer + g_rx_offset + 4;

            net_handle_packet(pkt, rx_len);

            g_rx_offset = (g_rx_offset + rx_len + 4 + 3) & ~3;
            if (g_rx_offset >= RTL_RX_BUF_SIZE)
                g_rx_offset = 0;

            outw(reg16(RTL_CAPR), (uint16_t) (g_rx_offset - 16));
            processed = 1;
            break;
        }
        return processed;
    }

    if (status & RTL_ISR_TOK) {
        outw(reg16(RTL_ISR), RTL_ISR_TOK);
    }

    return 0;
}

void rtl8139_get_mac(uint8_t *mac) {
    for (int i = 0; i < ETH_ALEN; i++)
        mac[i] = g_mac[i];
}

uint16_t rtl8139_io_base(void) {
    return g_io_base;
}
uint32_t rtl8139_read_reg32(uint8_t offset) {
    return inl((uint16_t) (g_io_base + offset));
}
uint32_t rtl8139_read_reg16(uint8_t offset) {
    return inw((uint16_t) (g_io_base + offset));
}

void rtl8139_print_packet(const void *data, int len) {
    if (len < 14) return;
    const eth_hdr_t *eth = (const eth_hdr_t *) data;

    serial_print("  dst=");
    for (int i = 0; i < 6; i++) { print_hex_byte(eth->dst[i]); if (i<5) serial_print(":"); }
    serial_print(" src=");
    for (int i = 0; i < 6; i++) { print_hex_byte(eth->src[i]); if (i<5) serial_print(":"); }
    serial_print(" type=0x");
    serial_print_hex(bswap16(eth->type));
    serial_print("\n");

    if (bswap16(eth->type) == ETH_P_ARP && len >= 42) {
        const arp_pkt_t *arp = (const arp_pkt_t *) (data + 14);
        serial_print("  ARP op=");
        serial_print_dec(bswap16(arp->op));
        serial_print(" spa=");
        serial_print_hex(bswap16(arp->spa >> 16));
        serial_print_hex(bswap16(arp->spa & 0xFFFF));
        serial_print("\n");
    }
}
