#include "libc.h"

static void write_num(uint64_t n) {
    char buf[24];
    char *p = buf + 23;
    *p = 0;
    if (n == 0) *--p = '0';
    while (n) { *--p = (char)('0' + (n % 10)); n /= 10; }
    sys_write(STDOUT_FILENO, p, (uint64_t)(buf + 23 - p));
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    sys_write(STDOUT_FILENO, "spin: loop infinito iniciado (sem yield)\n", 42);
    uint64_t i = 0;
    uint64_t rounds = 0;
    while (1) {
        i++;
        if (i % 50000000 == 0) {
            rounds++;
            sys_write(STDOUT_FILENO, "spin: round ", 12);
            write_num(rounds);
            sys_write(STDOUT_FILENO, "\n", 1);
            if (rounds >= 5) {
                sys_write(STDOUT_FILENO, "spin: saindo\n", 13);
                sys_exit(0);
            }
        }
    }
    return 0;
}
