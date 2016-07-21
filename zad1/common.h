#pragma once

#include <sys/types.h>
#include <stddef.h>

/** Round value up so that it is divisible by mult */
size_t round_up(size_t value, int mult);

/** pread until all bytes are read */
void read_all(int fd, char *buf, size_t sz, off_t offset);
