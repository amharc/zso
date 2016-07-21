#include "alloc.h"
#include "error.h"

#include <stdbool.h>
#include <sys/mman.h>

#define LENGTH 0x70000

static char buffer[LENGTH];
static size_t used;

void* alloc(size_t size) {
    EXPECT(used + size <= LENGTH);
    void *ret = buffer + used;
    used += size;
    return ret;
}
