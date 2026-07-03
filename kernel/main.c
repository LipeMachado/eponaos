/* kernel/main.c — nucleo do EponaOS (M3b: drivers VGA + serial) */
#include "serial.h"
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"

static void print_u64(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0)
        buf[--i] = '0';
    while (v) {
        buf[--i] = (char) ('0' + v % 10);
        v /= 10;
    }
    vga_print(&buf[i]);
    serial_print(&buf[i]);
}

void kernel_main(void) {
    serial_init();
    vga_init();
    gdt_init();
    idt_init();
    pic_remap();
    pit_init(100); /* 100 Hz */
    pmm_init();

    vga_set_color(0x0B, 0x00); /* ciano claro */
    vga_print("=== EponaOS ===\n");
    vga_set_color(0x0F, 0x00); /* branco */
    vga_print("Kernel em C, long mode 64-bit.\n");
    vga_print("Driver VGA: putc, print, newline, scroll, cor e cursor.\n\n");
    vga_print("Linha A\nLinha B\nLinha C\n");
    vga_print("GDT do kernel + TSS carregadas (ring0/ring3).\n");
    vga_print("IDT instalada (excecoes 0-31).\n");
    vga_print("RAM total: ");
    print_u64(pmm_total_bytes() / (1024 * 1024));
    vga_print(" MiB | livre: ");
    print_u64(pmm_free_bytes() / (1024 * 1024));
    vga_print(" MiB\n");

    /* teste: liberar e realocar deve reciclar o mesmo frame */
    void *a = pmm_alloc();
    void *b = pmm_alloc();
    pmm_free(a);
    void *c = pmm_alloc();
    vga_print("PMM: reciclou frame liberado? ");
    vga_print(a == c ? "SIM\n" : "NAO\n");
    (void) b;
    serial_print("[pmm] init + alloc/free ok.\n");

    __asm__ volatile("sti"); /* liga interrupcoes UMA vez, com tudo pronto */

    serial_print("[kernel] EponaOS iniciado.\n");
    serial_print("[kernel] VGA + serial COM1 prontos.\n");
    serial_print("[kernel] GDT recarregada, TR = 0x28.\n");

    vga_print("Interrupcoes ON: timer 100Hz + teclado.\n");
    vga_print("Digite algo: ");
    serial_print("[kernel] PIC/PIT/teclado prontos; sti executado.\n");

    for (;;) {
        __asm__ volatile("hlt");
    }
}
