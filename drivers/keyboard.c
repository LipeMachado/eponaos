#include "keyboard.h"
#include "io.h"
#include <stdint.h>

#define KEYBUF_SIZE 256

static int g_buf[KEYBUF_SIZE];
static volatile int g_head, g_tail;
static int g_shift, g_extended;
static uint8_t g_pressed[128];

/* scancode set 1 -> ASCII sem shift */
static const char g_map[128] = {0,   27,  '1',  '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
                                '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
                                'o', 'p', '[',  ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
                                'j', 'k', 'l',  ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
                                'b', 'n', 'm',  ',',  '.',  '/', 0,   '*',  0,   ' '};

/* scancode set 1 -> ASCII com shift */
static const char g_map_shift[128] = {0,   27,  '!',  '@',  '#',  '$', '%', '^',  '&', '*', '(', ')',
                                      '_', '+', '\b', '\t', 'Q',  'W', 'E',  'R',  'T', 'Y', 'U', 'I',
                                      'O', 'P', '{',  '}',  '\n', 0,   'A',  'S',  'D', 'F', 'G', 'H',
                                      'J', 'K', 'L',  ':',  '"',  '~', 0,    '|',  'Z', 'X', 'C', 'V',
                                      'B', 'N', 'M',  '<',  '>',  '?', 0,    '*',  0,   ' '};

/* scancodes extended (apos 0xE0) -> special keys */
static const int g_ext_map[128] = {
    [0x47] = KEY_HOME,  [0x48] = KEY_UP,    [0x49] = KEY_PGUP,
    [0x4B] = KEY_LEFT,  [0x4D] = KEY_RIGHT,
    [0x4F] = KEY_END,   [0x50] = KEY_DOWN,  [0x51] = KEY_PGDN,
    [0x52] = KEY_INS,   [0x53] = KEY_DEL,
};

static void put_key(int code) {
    int next = (g_tail + 1) % KEYBUF_SIZE;
    if (next != g_head) {
        g_buf[g_tail] = code;
        g_tail = next;
    }
}

void keyboard_irq(void) {
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {
        g_extended = 1;
        return;
    }

    if (g_extended) {
        g_extended = 0;
        uint8_t code_sc = sc & 0x7F;
        if (sc & 0x80) {
            g_pressed[code_sc] = 0;
            return;
        }
        if (g_pressed[code_sc]) return;
        g_pressed[code_sc] = 1;

        int code = g_ext_map[code_sc];
        if (code) put_key(code);
        return;
    }

    if (sc == 0x2A || sc == 0x36) { g_shift = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { g_shift = 0; return; }

    uint8_t code_sc = sc & 0x7F;
    if (sc & 0x80) {
        g_pressed[code_sc] = 0;
        return;
    }
    if (g_pressed[code_sc]) return;
    g_pressed[code_sc] = 1;

    char c = g_shift ? g_map_shift[code_sc] : g_map[code_sc];
    if (c) put_key((int)(unsigned char)c);
}

int keyboard_getc(void) {
    if (g_head == g_tail && (inb(0x64) & 1))
        keyboard_irq();

    if (g_head == g_tail) return 0;
    int code = g_buf[g_head];
    g_head = (g_head + 1) % KEYBUF_SIZE;
    return code;
}
