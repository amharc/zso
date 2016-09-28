#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

typedef void (*formatter)(int);

char fun[] = {
    0x55,                         /* pushl %ebp */
    0x89, 0xe5,                   /* movl %esp, %ebp */
    0x83, 0xec, 0x08,             /* subl $0x8, %esp */
    0x83, 0xec, 0x08,             /* subl $0x8, %esp */
    0xff, 0x75, 0x08,             /* pushl 0x8(%ebp) */
    0x68, 0x00, 0x00, 0x00, 0x00, /* pushl ADDR */
    0xe8, 0x00, 0x00, 0x00, 0x00, /* call ADDR */
    0x83, 0xc4, 0x10,             /* addl $0x10, %esp */
    0x90,                         /* nop */
    0xc9,                         /* leave */
    0xc3                          /* ret */
};

const int printf_offset = 18;
const int arg_offset = 13;
const int fun_size = sizeof(fun);

char *buffer;
char *buffer_end;
const size_t buffer_size = sizeof(fun) * 1024;

formatter make_formatter(const char *format) {
    if(buffer == buffer_end) {
        buffer = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(buffer == MAP_FAILED)
            perror("map");
        buffer_end = buffer + buffer_size;
    }

    char *res = buffer;
    buffer += fun_size;
    char *printf_addr = (void*)&printf;
    int32_t diff = printf_addr - (res + printf_offset + sizeof(void*));
    memcpy(res, fun, fun_size);
    memcpy(res + printf_offset, &diff, 4);
    memcpy(res + arg_offset, &format, 4);
    return (void*)res;
}

int main() {
    formatter x08_format = make_formatter ("%08x\n");
    formatter xalt_format = make_formatter ("%#x\n");
    formatter d_format = make_formatter ("%d\n");
    formatter verbose_format = make_formatter ("Liczba: %9d!\n");

    x08_format (0x1234);
    xalt_format (0x5678);
    d_format (0x9abc);
    verbose_format (0xdef0);
}
