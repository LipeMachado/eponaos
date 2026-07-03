#include "pit.h"
#include "io.h"

#define PIT_CH0  0x40
#define PIT_CMD  0x43
#define PIT_FREQ 1193182u

static volatile uint64_t g_ticks = 0;

void pit_init(uint32_t hz) {
    uint32_t divisor = PIT_FREQ / hz;
    outb(PIT_CMD, 0x36); /* canal 0, lo/hi byte, modo 3 (onda quadrada) */
    outb(PIT_CH0, (uint8_t) (divisor & 0xFF));
    outb(PIT_CH0, (uint8_t) ((divisor >> 8) & 0xFF));
}

void pit_tick(void) {
    g_ticks++;
}

uint64_t pit_ticks(void) {
    return g_ticks;
}
