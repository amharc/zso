#include "common.h"
#include "error.h"
#include "syscalls.h"

size_t round_up(size_t value, int mult) {
    if(value % mult == 0)
        return value;
    else
        return value + mult - value % mult;
}

void read_all(int fd, char *buf, size_t sz, off_t offset) {
    while(sz > 0) {
        ssize_t ret = raw_pread(fd, buf, sz, offset);
        EXPECT(ret > 0);
        buf += ret;
        sz -= ret;
        offset += ret;
    }
}
