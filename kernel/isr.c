#include "serial.h"
#include "vga.h"
#include <stdint.h>
#include "keyboard.h"
#include "mouse.h"
#include "pic.h"
#include "pit.h"
#include "scheduler.h"

struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

static const char *g_exc_names[32] = {"Divide-by-zero",
                                      "Debug",
                                      "NMI",
                                      "Breakpoint",
                                      "Overflow",
                                      "BOUND Range",
                                      "Invalid Opcode",
                                      "Device N/A",
                                      "Double Fault",
                                      "Coproc Overrun",
                                      "Invalid TSS",
                                      "Segment N/P",
                                      "Stack Fault",
                                      "General Protection",
                                      "Page Fault",
                                      "Reserved",
                                      "x87 FP",
                                      "Alignment Check",
                                      "Machine Check",
                                      "SIMD FP",
                                      "Virtualization",
                                      "Control Prot.",
                                      "Reserved",
                                      "Reserved",
                                      "Reserved",
                                      "Reserved",
                                      "Reserved",
                                      "Reserved",
                                      "Hypervisor",
                                      "VMM Comm",
                                      "Security",
                                      "Reserved"};

void isr_handler(struct regs *r) {
    vga_set_color(0x0C, 0x00); /* vermelho claro */
    vga_print("\n[EXCECAO] ");
    if (r->vector < 32)
        vga_print(g_exc_names[r->vector]);
    vga_print("\n");
    vga_set_color(0x0F, 0x00);

    serial_print("[EXCECAO] ");
    if (r->vector < 32)
        serial_print(g_exc_names[r->vector]);
    serial_print("\n");

    /* breakpoint (int3): so avisa e continua; falhas reais: trava */
    if (r->vector == 3)
        return;
    for (;;)
        __asm__ volatile("cli; hlt");
}

void irq_handler(struct regs *r) {
    int irq = (int) r->vector - 32;
    if (irq == 0) {
        pit_tick();
        pic_send_eoi(irq);
        schedule();
        return;
    }
    else if (irq == 1)
        keyboard_irq();
    else if (irq == 12)
        mouse_irq();
    pic_send_eoi(irq);
}
