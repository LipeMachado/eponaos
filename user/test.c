#include "libc.h"

int main(int argc, char **argv) {
    (void)argv;

    sys_write(STDOUT_FILENO, "Hello from C in ring 3!\n", 24);
    sys_write(STDOUT_FILENO, "argc = ", 7);
    char buf[16];
    int n = argc;
    char *p = buf + 15;
    *p = 0;
    if (n == 0) *--p = '0';
    while (n) { *--p = '0' + n % 10; n /= 10; }
    sys_write(STDOUT_FILENO, p, (uint64_t)(buf + 15 - p));
    sys_write(STDOUT_FILENO, "\n", 1);

    sys_write(STDOUT_FILENO, "Before sys_open\n", 16);
    int fd = sys_open("hello.txt");
    sys_write(STDOUT_FILENO, "After sys_open\n", 15);

    if (fd >= 0) {
        char rbuf[128];
        int nr = sys_read(fd, rbuf, 127);
        if (nr > 0) {
            rbuf[nr] = 0;
            sys_write(STDOUT_FILENO, "Content: ", 9);
            sys_write(STDOUT_FILENO, rbuf, (uint64_t)nr);
            sys_write(STDOUT_FILENO, "\n", 1);
        }
        sys_close(fd);
    } else {
        sys_write(STDOUT_FILENO, "Failed to open hello.txt\n", 25);
    }

    return 0;
}
