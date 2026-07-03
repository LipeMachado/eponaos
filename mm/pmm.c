#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE       4096
#define E820_COUNT_ADDR 0x4000
#define E820_ENTRY_ADDR 0x4004
#define MAX_FRAMES      (1024 * 1024)     /* suporta ate 4 GiB */
#define RESERVE_LOW     (2 * 1024 * 1024) /* 1os 2 MiB: BIOS + boot + kernel */

struct e820_entry {
    uint64_t base;
    uint64_t len;
    uint32_t type; /* 1 = RAM usavel */
    uint32_t acpi;
} __attribute__((packed));

static uint8_t g_bitmap[MAX_FRAMES / 8]; /* 1 bit por frame (0=livre,1=usado) */
static uint64_t g_total_frames;
static uint64_t g_used_frames;

static void bm_set(uint64_t f) {
    g_bitmap[f >> 3] |= (uint8_t) (1u << (f & 7));
}
static void bm_clear(uint64_t f) {
    g_bitmap[f >> 3] &= (uint8_t) ~(1u << (f & 7));
}
static int bm_test(uint64_t f) {
    return g_bitmap[f >> 3] & (1u << (f & 7));
}

void pmm_init(void) {
    for (size_t i = 0; i < sizeof(g_bitmap); i++)
        g_bitmap[i] = 0xFF; /* comeca tudo reservado */
    g_total_frames = 0;

    uint32_t count = *(volatile uint32_t *) E820_COUNT_ADDR;
    struct e820_entry *e = (struct e820_entry *) E820_ENTRY_ADDR;

    /* libera as faixas usaveis (type==1) */
    for (uint32_t i = 0; i < count; i++) {
        if (e[i].type != 1)
            continue;
        uint64_t start = (e[i].base + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t stop = (e[i].base + e[i].len) / PAGE_SIZE;
        for (uint64_t f = start; f < stop && f < MAX_FRAMES; f++)
            bm_clear(f);
        if (stop > g_total_frames)
            g_total_frames = stop;
    }
    if (g_total_frames > MAX_FRAMES)
        g_total_frames = MAX_FRAMES;

    /* reserva os 2 primeiros MiB (BIOS, tabelas do boot, imagem do kernel) */
    for (uint64_t f = 0; f < RESERVE_LOW / PAGE_SIZE; f++)
        bm_set(f);

    g_used_frames = 0;
    for (uint64_t f = 0; f < g_total_frames; f++)
        if (bm_test(f))
            g_used_frames++;
}

void *pmm_alloc(void) {
    for (uint64_t f = 0; f < g_total_frames; f++) {
        if (!bm_test(f)) {
            bm_set(f);
            g_used_frames++;
            return (void *) (f * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free(void *frame) {
    uint64_t f = (uint64_t) frame / PAGE_SIZE;
    if (f < g_total_frames && bm_test(f)) {
        bm_clear(f);
        g_used_frames--;
    }
}

uint64_t pmm_total_bytes(void) {
    return g_total_frames * PAGE_SIZE;
}
uint64_t pmm_free_bytes(void) {
    return (g_total_frames - g_used_frames) * PAGE_SIZE;
}
