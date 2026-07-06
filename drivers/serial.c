#include "serial.h"
#include "io.h"
#include <stddef.h>
#include <stdint.h>

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); /* desliga interrupcoes da UART */
    outb(COM1 + 3, 0x80); /* DLAB=1: acessa o divisor de baud */
    outb(COM1 + 0, 0x03); /* divisor baixo = 3  -> 38400 baud */
    outb(COM1 + 1, 0x00); /* divisor alto = 0 */
    outb(COM1 + 3, 0x03); /* DLAB=0: 8 bits, sem paridade, 1 stop */
    outb(COM1 + 2, 0xC7); /* liga+limpa FIFO, threshold 14 bytes */
    outb(COM1 + 4, 0x0B); /* DTR, RTS, OUT2 ligados */
}

static int serial_tx_ready(void) {
    return inb(COM1 + 5) & 0x20; /* bit5 = registrador de transmissao vazio */
}

void serial_putc(char c) {
    if (c == '\n')
        serial_putc('\r'); /* terminal espera CR+LF */
    while (!serial_tx_ready()) {
    }
    outb(COM1, (uint8_t) c);
}

void serial_print(const char *s) {
    for (size_t i = 0; s[i]; i++)
        serial_putc(s[i]);
}

static const char hex_chars[] = "0123456789ABCDEF";

void serial_print_hex(uint64_t v) {
    char buf[19] = "0x";
    int pos = 2;
    int started = 0;
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (uint8_t) ((v >> (i * 4)) & 0xF);
        if (nibble || started || i == 0) {
            buf[pos++] = hex_chars[nibble];
            started = 1;
        }
    }
    buf[pos] = '\0';
    serial_print(buf);
}

void serial_print_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0)
        buf[--i] = '0';
    while (v) {
        buf[--i] = (char) ('0' + v % 10);
        v /= 10;
    }
    serial_print(&buf[i]);
}
