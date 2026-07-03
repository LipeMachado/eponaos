#include "keyboard.h"
#include "io.h"
#include "vga.h"
#include <stdint.h>

/* scancode set 1 -> ASCII (layout US, sem shift). 0 = ignora */
static const char g_map[128] = {0,   27,  '1',  '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
                                '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
                                'o', 'p', '[',  ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
                                'j', 'k', 'l',  ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
                                'b', 'n', 'm',  ',',  '.',  '/', 0,   '*',  0,   ' '};

void keyboard_irq(void) {
    uint8_t sc = inb(0x60);
    if (sc & 0x80)
        return; /* bit7 = tecla SOLTA -> ignora */
    char c = g_map[sc & 0x7F];
    if (c)
        vga_putc(c);
}
