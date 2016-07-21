#include "v2d_utils.h"

void v2d_wait_for_context(struct v2d_context *context) {
    struct v2d_command *command;

    spin_lock_bh(&context->lock);
    if (list_empty(&context->commands)) {
        spin_unlock_bh(&context->lock);
        return;
    }

    command = list_last_entry(&context->commands, struct v2d_command, context_list);
    v2d_get_command(command);
    spin_unlock_bh(&context->lock);

    wait_event(command->queue, atomic_read(&command->ready));

    v2d_put_command(command);
}

int v2d_wait_for_context_interruptible(struct v2d_context *context) {
    struct v2d_command *command;
    int ret;

    spin_lock_bh(&context->lock);
    if (list_empty(&context->commands)) {
        spin_unlock_bh(&context->lock);
        return 0;
    }

    command = list_last_entry(&context->commands, struct v2d_command, context_list);
    v2d_get_command(command);
    spin_unlock_bh(&context->lock);

    ret = wait_event_interruptible(command->queue, atomic_read(&command->ready));

    v2d_put_command(command);

    return ret;
}

void *v2d_alloc_page(struct v2d_device *device, dma_addr_t *addr) {
    void *ret = dma_pool_alloc(device->dma_pool, GFP_KERNEL, addr);
    if (unlikely(!ret)) {
        return NULL;
    }

    BUG_ON(*addr & (V2D_PAGESIZE - 1));

    memset(ret, 0, V2D_PAGESIZE);
    return ret;
}

void v2d_debug(const struct v2d_device *device) {
    size_t write_index = (ioread32(device->bar0 + VINTAGE2D_CMD_WRITE_PTR) - device->dma_cmd_buf) / sizeof(uint32_t);
    size_t read_index = (ioread32(device->bar0 + VINTAGE2D_CMD_READ_PTR) - device->dma_cmd_buf) / sizeof(uint32_t);
    size_t status = ioread32(device->bar0 + VINTAGE2D_STATUS);
    size_t intr = ioread32(device->bar0 + VINTAGE2D_INTR);
    printk(KERN_DEBUG V2D_PREFIX "write=%zd, read=%zd, status=%zd, intr=%zd\n", write_index, read_index, status, intr);
}
