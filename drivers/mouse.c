#include "mouse.h"
#include "io.h"
#include "serial.h"
#include <stddef.h>

#define PS2_CMD   0x64
#define PS2_DATA  0x60

#define MOUSE_CMD_ENABLE  0xF4
#define MOUSE_CMD_SAMPLE  0xF3
#define MOUSE_CMD_DEFAULTS 0xF6

static int g_mouse_x = 0;
static int g_mouse_y = 0;
static uint8_t g_mouse_cycle = 0;
static uint8_t g_mouse_packet[3];

static int ps2_wait_write(void) {
    for (int i = 0; i < 10000; i++) {
        if (!(inb(PS2_CMD) & 0x02))
            return 0;
    }
    return -1;
}

static int ps2_wait_read(void) {
    for (int i = 0; i < 10000; i++) {
        if (inb(PS2_CMD) & 0x01)
            return 0;
    }
    return -1;
}

static int mouse_send_cmd(uint8_t cmd) {
    if (ps2_wait_write() < 0) return -1;
    outb(PS2_CMD, 0xD4);
    if (ps2_wait_write() < 0) return -1;
    outb(PS2_DATA, cmd);
    if (ps2_wait_read() < 0) return -1;
    uint8_t ack = inb(PS2_DATA);
    return (ack == 0xFA) ? 0 : -1;
}

void mouse_init(void) {
    serial_print("[mouse] init...\n");

    if (ps2_wait_write() < 0) return;
    outb(PS2_CMD, 0xA8);

    if (ps2_wait_write() < 0) return;
    outb(PS2_CMD, 0x20);
    if (ps2_wait_read() < 0) return;
    uint8_t cfg = inb(PS2_DATA);
    cfg |= 0x02;
    cfg &= ~0x20;
    if (ps2_wait_write() < 0) return;
    outb(PS2_CMD, 0x60);
    if (ps2_wait_write() < 0) return;
    outb(PS2_DATA, cfg);

    mouse_send_cmd(MOUSE_CMD_DEFAULTS);
    mouse_send_cmd(MOUSE_CMD_SAMPLE);
    mouse_send_cmd(100);
    mouse_send_cmd(MOUSE_CMD_ENABLE);

    g_mouse_cycle = 0;
    serial_print("[mouse] ready\n");
}

void mouse_irq(void) {
    uint8_t data = inb(PS2_DATA);

    switch (g_mouse_cycle) {
    case 0:
        if (!(data & 0x08)) {
            g_mouse_cycle = 0;
            return;
        }
        g_mouse_packet[0] = data;
        g_mouse_cycle = 1;
        break;
    case 1:
        g_mouse_packet[1] = data;
        g_mouse_cycle = 2;
        break;
    case 2:
        g_mouse_packet[2] = data;
        g_mouse_cycle = 0;

        int dx = (int) (int8_t) g_mouse_packet[1];
        int dy = (int) (int8_t) g_mouse_packet[2];
        if (g_mouse_packet[0] & 0x40) dx = -dx;
        if (g_mouse_packet[0] & 0x80) dy = -dy;
        g_mouse_x += dx;
        g_mouse_y += dy;

        serial_print("[mouse] x=");
        serial_print_dec(g_mouse_x);
        serial_print(" y=");
        serial_print_dec(g_mouse_y);
        serial_print(" b=");
        serial_print_hex(g_mouse_packet[0] & 7);
        serial_print("\n");
        break;
    }
}
