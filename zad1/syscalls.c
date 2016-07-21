#include "syscalls.h"
#include "raw_syscall.h"

#include <stdlib.h>
#include <asm/unistd.h>
#include <linux/unistd.h>
#include <asm/ldt.h>

void* raw_mmap2(void *addr, size_t len, int prot, int flags, int filedes, off_t off) {
    return (void*)raw_syscall(__NR_mmap2, (int)addr, len, prot, flags, filedes, off);
}

int raw_open(const char *pathname, int flags) {
    return raw_syscall(__NR_open, (int)pathname, flags, 0, 0, 0, 0);
}

int raw_close(int fd) {
    return raw_syscall(__NR_close, fd, 0, 0, 0, 0, 0);
}

int raw_mprotect(void *addr, size_t len, int prot) {
    return raw_syscall(__NR_mprotect, (int)addr, len, prot, 0, 0, 0);
}

int raw_set_thread_area(struct user_desc *u_info) {
    return raw_syscall(__NR_set_thread_area, (int)u_info, 0, 0, 0, 0, 0);
}

ssize_t raw_write(int fd, char *msg, size_t len) {
    return raw_syscall(__NR_write, fd, (int)msg, len, 0, 0, 0);
}

_Noreturn void raw_exit(int retcode) {
    raw_syscall(__NR_exit, retcode, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

ssize_t raw_pread(int fd, void *buf, size_t nbyte, off_t offset) {
    return raw_syscall(__NR_pread64, fd, (int)buf, nbyte, offset, 0, 0);
}
