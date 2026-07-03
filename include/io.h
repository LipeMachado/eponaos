#ifndef EPONA_IO_H
#define EPONA_IO_H
#include <stdint.h>

/* escreve um byte numa porta de I/O */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
/* le um byte de uma porta de I/O */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#endif
