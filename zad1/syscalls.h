#pragma once

#include <stdlib.h>
#include <asm/ldt.h>

/** We don't want to call the VDSO, because it may get overwritten by mmap.
 *  Therefore, syscalls are called through int $0x80.
 *
 *  Moreover, glibc wrappers often perform not only the respective syscall,
 *  e.g. close is a cancellation point. Also debugging is far easier with
 *  hand-written syscall wrappers.
 */

void* raw_mmap2(void *addr, size_t len, int prot, int flags, int filedes, off_t off);

int raw_open(const char *pathname, int flags);

int raw_close(int fd);

int raw_mprotect(void *addr, size_t len, int prot);

int raw_set_thread_area(struct user_desc *u_info);

ssize_t raw_write(int fd, char *msg, size_t len);

_Noreturn void raw_exit(int retcode);

ssize_t raw_pread(int fd, void *buf, size_t nbyte, off_t offset);
