#ifndef EPONA_RTL8139_H
#define EPONA_RTL8139_H

#include <stdint.h>

static inline uint16_t bswap16(uint16_t v) {
    return (v << 8) | (v >> 8);
}
static inline uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000FF) << 24) |
           ((v & 0x0000FF00) << 8) |
           ((v & 0x00FF0000) >> 8) |
           ((v & 0xFF000000) >> 24);
}

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

#define RTL_MAC      0x00
#define RTL_TSD0     0x10
#define RTL_TSD1     0x14
#define RTL_TSD2     0x18
#define RTL_TSD3     0x1C
#define RTL_TSAD0    0x20
#define RTL_TSAD1    0x24
#define RTL_TSAD2    0x28
#define RTL_TSAD3    0x2C
#define RTL_RBSTART  0x30
#define RTL_CR       0x37
#define RTL_CAPR     0x38
#define RTL_IMR      0x3C
#define RTL_ISR      0x3E
#define RTL_TCR      0x40
#define RTL_RCR      0x44
#define RTL_9346CR   0x50
#define RTL_CONFIG1  0x52

#define RTL_CR_RST   0x10
#define RTL_CR_RE    0x08
#define RTL_CR_TE    0x04

#define RTL_RCR_AAP  0x01
#define RTL_RCR_APM  0x02
#define RTL_RCR_AM   0x04
#define RTL_RCR_AB   0x08
#define RTL_RCR_WRAP 0x80

#define RTL_ISR_ROK  0x01
#define RTL_ISR_RER  0x02
#define RTL_ISR_TOK  0x04
#define RTL_ISR_TER  0x08

#define RTL_TX_BUFS  4
#define RTL_RX_BUF_SIZE (8192 + 16)

#define ETH_ALEN 6
#define ETH_P_ARP 0x0806
#define ETH_P_IP  0x0800

typedef struct {
    uint8_t dst[ETH_ALEN];
    uint8_t src[ETH_ALEN];
    uint16_t type;
} __attribute__((packed)) eth_hdr_t;

typedef struct {
    uint16_t hw_type;
    uint16_t proto;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t op;
    uint8_t  sha[ETH_ALEN];
    uint32_t spa;
    uint8_t  tha[ETH_ALEN];
    uint32_t tpa;
} __attribute__((packed)) arp_pkt_t;

int rtl8139_init(void);
int rtl8139_send(const void *data, int len);
int rtl8139_poll(void);
void rtl8139_get_mac(uint8_t *mac);
void rtl8139_print_packet(const void *data, int len);

/* utilitarios para acessar registros de fora do driver */
uint16_t rtl8139_io_base(void);
uint32_t rtl8139_read_reg32(uint8_t offset);
uint32_t rtl8139_read_reg16(uint8_t offset);

#endif
