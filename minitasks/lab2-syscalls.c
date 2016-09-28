/* Measure perfomance of int 80h vs sysenter */
#include <stdio.h>
#include <asm/unistd.h>
#include <time.h>

void syscall_int80h(void) {
    register int fake_eax asm("eax");
    asm volatile("int $0x80" : "=r"(fake_eax) : "a"(__NR_getpid) : "%ebx", "cc");
}

void syscall_sysenter(void) {
    register int fake_eax asm("eax");
    asm volatile("pushl $out%=\n"
                 "pushl %%ecx\n"
                 "pushl %%edx\n"
                 "pushl %%ebp\n"
                 "movl %%esp, %%ebp\n"
                 "sysenter\n"
                 "out%=:\n"
                 : "=r"(fake_eax) : "a"(__NR_getpid) : "%ebx", "cc");

    /* asm volatile("call *%%gs:0x10" :: "a"(__NR_getpid) : "%ebx"); */
}

#define MEASURE(name, func) \
{ \
    struct timespec start, stop; \
\
    printf("Measuring %s\n", name); \
    clock_gettime(CLOCK_REALTIME, &start); \
    for(int i = 0; i < 10000000; ++i) \
        func(); \
    clock_gettime(CLOCK_REALTIME, &stop); \
    printf("Took: %f ms\n", 1e3 * (stop.tv_sec - start.tv_sec) + (stop.tv_nsec - start.tv_nsec) * 1e-6); \
} \

int main() {
    MEASURE("int 80h", syscall_int80h);
    MEASURE("sysenter", syscall_sysenter);
    return 0;
}
