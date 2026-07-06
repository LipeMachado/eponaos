#ifndef EPONA_KEYBOARD_H
#define EPONA_KEYBOARD_H

#define KEY_UP     256
#define KEY_DOWN   257
#define KEY_LEFT   258
#define KEY_RIGHT  259
#define KEY_HOME   260
#define KEY_END    261
#define KEY_DEL    262
#define KEY_PGUP   263
#define KEY_PGDN   264
#define KEY_INS    265

void keyboard_irq(void);
int  keyboard_getc(void);

#endif
