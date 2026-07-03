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
