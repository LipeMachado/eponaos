#include "pic.h"
#include "io.h"
#include <stdint.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

void pic_remap(void) {
    outb(PIC1_CMD, 0x11); /* ICW1: iniciar + esperar ICW4 */
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20); /* ICW2: mestre -> vetores 32..39 */
    outb(PIC2_DATA, 0x28); /* ICW2: escravo -> vetores 40..47 */
    outb(PIC1_DATA, 0x04); /* ICW3: escravo ligado no IRQ2 do mestre */
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01); /* ICW4: modo 8086 */
    outb(PIC2_DATA, 0x01);

    /* mascaras: IRQ0 (timer), IRQ1 (teclado), IRQ2 (cascade p/ escravo) no mestre */
    outb(PIC1_DATA, 0xF8); /* 1111 1000 */
    outb(PIC2_DATA, 0xEF); /* 1110 1111 -> IRQ12 (mouse) ligado */
}

void pic_send_eoi(int irq) {
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}
