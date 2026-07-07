#ifndef EPONA_MOUSE_H
#define EPONA_MOUSE_H

#include <stdint.h>

void mouse_init(void);
void mouse_irq(void);
void mouse_get_state(int *x, int *y, uint8_t *buttons);
int mouse_is_available(void);

#endif
