#pragma once
#include "v2d_common.h"

/* Waits till all currently scheduled writes are finished */
void v2d_wait_for_context(struct v2d_context *context);

/* Waits till all currently scheduled writes are finished.
 * Interruptible by signals.
 */
int v2d_wait_for_context_interruptible(struct v2d_context *context);

/* Allocs a zeroed, DMA-consistent page */
void *v2d_alloc_page(struct v2d_device *device, dma_addr_t *addr);

/* Logs some debug info about the device */
void v2d_debug(const struct v2d_device *device);

static inline void v2d_get_command(struct v2d_command *command) {
    atomic_inc(&command->refcount);
    smp_mb();
}

static inline void v2d_put_command(struct v2d_command *command) {
    smp_mb();
    if (atomic_dec_and_test(&command->refcount)) {
        kfree(command);
    }
}

static inline ssize_t v2d_space_own(const struct v2d_device *device, size_t write_index) {
    return CIRC_SPACE(write_index, ACCESS_ONCE(device->read_index), V2D_CMDBUF_SIZE);
}

static inline ssize_t v2d_space(const struct v2d_device *device) {
    return v2d_space_own(device, ACCESS_ONCE(device->write_index));
}

static inline ssize_t v2d_space_own_end(const struct v2d_device *device, size_t write_index) {
    size_t read_index = ACCESS_ONCE(device->read_index);
    if (write_index < read_index) {
        return CIRC_SPACE_TO_END(write_index, read_index, V2D_CMDBUF_SIZE);
    } else {
        // Do not allow to overwrite the final JUMP
        return CIRC_SPACE_TO_END(write_index, read_index, V2D_CMDBUF_SIZE) - 1;
    }
}

static inline ssize_t v2d_space_end(const struct v2d_device *device) {
    return v2d_space_own_end(device, ACCESS_ONCE(device->write_index));
}
