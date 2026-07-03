#include "idt.h"
#include <stdint.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry g_idt[256];
static struct idt_ptr g_idt_ptr;

extern void idt_load(struct idt_ptr *ptr);
extern void *isr_stub_table[]; /* enderecos dos stubs (isr.asm) */

static void idt_set(int vec, void *handler, uint8_t type_attr) {
    uint64_t addr = (uint64_t) handler;
    g_idt[vec].offset_low = addr & 0xFFFF;
    g_idt[vec].selector = 0x08; /* kernel code */
    g_idt[vec].ist = 0;
    g_idt[vec].type_attr = type_attr; /* 0x8E = present, ring0, interrupt gate */
    g_idt[vec].offset_mid = (addr >> 16) & 0xFFFF;
    g_idt[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    g_idt[vec].zero = 0;
}

void idt_init(void) {
    for (int i = 0; i < 32; i++)
        idt_set(i, isr_stub_table[i], 0x8E);

    extern void *irq_stub_table[];
    for (int i = 0; i < 16; i++)
        idt_set(32 + i, irq_stub_table[i], 0x8E);

    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base = (uint64_t) &g_idt;
    idt_load(&g_idt_ptr);
}
