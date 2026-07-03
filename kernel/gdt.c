#include "gdt.h"
#include <stddef.h>
#include <stdint.h>

/* ponteiro que o lgdt le: limite (tam-1) + base (endereco da tabela) */
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* TSS de 64 bits (104 bytes) — so usamos rsp0 por enquanto */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

/* 5 descritores normais + 2 slots do descritor de TSS = 7 qwords */
static uint64_t g_gdt[7];
static struct tss g_tss;
static uint8_t g_kernel_stack[16384] __attribute__((aligned(16)));

extern void gdt_flush(struct gdt_ptr *ptr);
extern void tss_flush(uint64_t selector);

/* monta o descritor de TSS (system, 16 bytes -> ocupa 2 entradas) */
static void gdt_set_tss(int idx, uint64_t base, uint32_t limit) {
    uint64_t low = 0;
    low |= (uint64_t) (limit & 0xFFFF);
    low |= (uint64_t) (base & 0xFFFFFF) << 16;
    low |= (uint64_t) 0x89 << 40; /* present, tipo=TSS 64-bit */
    low |= (uint64_t) ((limit >> 16) & 0xF) << 48;
    low |= (uint64_t) ((base >> 24) & 0xFF) << 56;
    g_gdt[idx] = low;
    g_gdt[idx + 1] = (base >> 32) & 0xFFFFFFFF; /* 32 bits altos da base */
}

void gdt_init(void) {
    g_gdt[0] = 0x0000000000000000; /* null */
    g_gdt[1] = 0x00AF9A000000FFFF; /* 0x08 kernel code (ring0) */
    g_gdt[2] = 0x00AF92000000FFFF; /* 0x10 kernel data */
    g_gdt[3] = 0x00AFFA000000FFFF; /* 0x18 user code  (ring3) */
    g_gdt[4] = 0x00AFF2000000FFFF; /* 0x20 user data  (ring3) */

    g_tss.rsp0 = (uint64_t) (g_kernel_stack + sizeof(g_kernel_stack));
    g_tss.iomap_base = sizeof(struct tss);
    gdt_set_tss(5, (uint64_t) &g_tss, sizeof(struct tss) - 1); /* 0x28 */

    struct gdt_ptr ptr = {
        .limit = sizeof(g_gdt) - 1,
        .base = (uint64_t) &g_gdt,
    };
    gdt_flush(&ptr);
    tss_flush(0x28);
}
