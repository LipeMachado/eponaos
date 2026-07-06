#include "libc.h"

static void print_dec(int v) {
    char buf[16];
    char *p = buf + 15;
    *p = 0;
    if (v == 0) *--p = '0';
    while (v) { *--p = '0' + v % 10; v /= 10; }
    sys_write(STDOUT_FILENO, p, (uint64_t)(buf + 15 - p));
}

int main(int argc, char **argv) {
    sys_write(STDOUT_FILENO, "Test 3 hello!\n", 14);
    sys_write(STDOUT_FILENO, "argc=", 5);
    print_dec(argc);
    sys_write(STDOUT_FILENO, "\n", 1);

    for (int i = 0; i < argc; i++) {
        sys_write(STDOUT_FILENO, "  argv[", 7);
        print_dec(i);
        sys_write(STDOUT_FILENO, "] = ", 4);
        int len = 0;
        while (argv[i][len]) len++;
        sys_write(STDOUT_FILENO, argv[i], (uint64_t)len);
        sys_write(STDOUT_FILENO, "\n", 1);
    }

    sys_exit(0);
    return 0;
}
