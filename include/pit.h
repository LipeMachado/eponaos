#ifndef EPONA_PIT_H
#define EPONA_PIT_H
#include <stdint.h>
void pit_init(uint32_t hz);
void pit_tick(void);
uint64_t pit_ticks(void);
#endif
