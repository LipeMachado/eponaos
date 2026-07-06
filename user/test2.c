#include "libc.h"

int main(int argc, char **argv) {
    sys_write(STDOUT_FILENO, "Hello from test2!\n", 18);
    sys_write(STDOUT_FILENO, "Calling sys_open...\n", 20);

    int fd = sys_open("hello.txt");
    
    sys_write(STDOUT_FILENO, "After sys_open, fd=", 19);
    
    char buf[16];
    char *p = buf + 15;
    *p = 0;
    int n = fd;
    if (n == 0) *--p = '0';
    while (n) { *--p = '0' + n % 10; n /= 10; }
    sys_write(STDOUT_FILENO, p, (uint64_t)(buf + 15 - p));
    sys_write(STDOUT_FILENO, "\n", 1);

    sys_exit(0);
    return 0;
}
