#ifndef EPONA_PMM_H
#define EPONA_PMM_H
#include <stdint.h>

void pmm_init(void);
void *pmm_alloc(void); /* aloca 1 frame de 4 KiB (fisico); NULL se acabar */
void pmm_free(void *frame);
uint64_t pmm_total_bytes(void);
uint64_t pmm_free_bytes(void);
#endif
