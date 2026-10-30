#include <errno.h>

// just for supress gcc warnings

int _read(int file, char* ptr, int len) {
    (void)file;
    (void)ptr;
    (void)len;
    errno = ENOSYS;
    return -1;
}

int _write(int file, char* ptr, int len) {
    (void)file;
    (void)ptr;
    return len;
}

int _close(int file) {
    (void)file;
    errno = EBADF;
    return -1;
}

int _lseek(int file, int ptr, int dir) {
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}
