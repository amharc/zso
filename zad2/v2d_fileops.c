#include "v2d_fileops.h"
#include "v2d_utils.h"
#include "v2d_validate.h"

static int v2d_open(struct inode *inode, struct file *file) {
    struct v2d_device *device;
    struct v2d_context *context;
    int ret;

    if (unlikely(!inode->i_cdev)) {
        return -ENXIO;
    }

    device = container_of(inode->i_cdev, struct v2d_device, cdev);
    context = kzalloc(sizeof(*context), GFP_KERNEL);
    if (unlikely(!context)) {
        ret = -ENOMEM;
        goto err_kmalloc_context;
    }

    context->device = device;
    context->width = context->height = V2D_DIMENSIONS_NOT_SET;
    context->state.src_pos_cmd = V2D_INVALID_COMMAND;
    context->state.dst_pos_cmd = V2D_INVALID_COMMAND;
    context->state.fill_color_cmd = V2D_INVALID_COMMAND;
    INIT_LIST_HEAD(&context->commands);
    spin_lock_init(&context->lock);

    context->pages.entries = v2d_alloc_page(device, &context->pages.dma_addr);
    if (unlikely(!context->pages.entries)) {
        ret = -ENOMEM;
        goto err_v2d_alloc_page;
    }

    file->private_data = context;

    return 0;

    dma_pool_free(device->dma_pool, context->pages.entries, context->pages.dma_addr);
err_v2d_alloc_page:
    kfree(context);
err_kmalloc_context:
    return ret;
}

static int v2d_release(struct inode *inode, struct file *file) {
    struct v2d_context *context = file->private_data, *tmp;
    struct v2d_device *device = context->device;
    size_t i;

    v2d_wait_for_context(context);

    /* Unset the device's last context, if it was set to `context' */
    tmp = NULL;
    cmpxchg(&device->last_context, context, tmp);

    /* Free the pages */
    for (i = 0; i < V2D_PAGES; ++i) {
        if (context->pages.entries[i] & VINTAGE2D_PTE_VALID) {
            dma_pool_free(device->dma_pool, context->pages.pages[i].cpu, context->pages.pages[i].dma);
        }
    }

    /* Free the page table */
    dma_pool_free(device->dma_pool, context->pages.entries, context->pages.dma_addr);

    kfree(context);

    return 0;
}

static long v2d_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct v2d_context *context = file->private_data;
    struct v2d_ioctl_set_dimensions dimensions;
    int ret;
    size_t page_cnt, page_total;

    if (cmd != V2D_IOCTL_SET_DIMENSIONS) {
        return -ENOTTY;
    }

    if (copy_from_user(&dimensions, (struct v2d_ioctl_set_dimensions *)arg, sizeof(dimensions))) {
        return -EFAULT;
    }

    if (dimensions.height < V2D_MIN_DIMENSION || dimensions.height > V2D_MAX_DIMENSION) {
        return -EINVAL;
    }

    if (dimensions.width < V2D_MIN_DIMENSION || dimensions.width > V2D_MAX_DIMENSION) {
        return -EINVAL;
    }

    spin_lock_bh(&context->lock);
    if (context->width != V2D_DIMENSIONS_NOT_SET) {
        spin_unlock_bh(&context->lock);
        return -EINVAL;
    }
    context->width = dimensions.width;
    context->height = dimensions.height;
    spin_unlock_bh(&context->lock);

    page_total = ((size_t)dimensions.width * dimensions.height + V2D_PAGESIZE - 1) / V2D_PAGESIZE;
    for (page_cnt = 0; page_cnt < page_total; ++page_cnt) {
        context->pages.pages[page_cnt].cpu = v2d_alloc_page(context->device, &context->pages.pages[page_cnt].dma);
        if (!context->pages.pages[page_cnt].cpu) {
            ret = -ENOMEM;
            goto err_v2d_alloc_page;
        }
        context->pages.pages[page_cnt].page = virt_to_page(context->pages.pages[page_cnt].cpu);
    }

    for (page_cnt = 0; page_cnt < page_total; ++page_cnt) {
        context->pages.entries[page_cnt] = context->pages.pages[page_cnt].dma | VINTAGE2D_PTE_VALID;
    }

    mb();

    /** The same pagetable could have been used previously by another
     * context, so we must reset the TLB
     * before reusing the same page table. It is more convenient to do this
     * here and not in v2d_release,
     * because the TLB must also be flushed before starting the first
     * writing operation.
     */
    iowrite32(VINTAGE2D_RESET_TLB, context->device->bar0 + VINTAGE2D_RESET);
    atomic_set(&context->ready, 1);

    smp_mb();

    return 0;

err_v2d_alloc_page:
    while (page_cnt > 0) {
        --page_cnt;
        dma_pool_free(context->device->dma_pool, context->pages.pages[page_cnt].cpu,
                      context->pages.pages[page_cnt].dma);
    }

    spin_lock_bh(&context->lock);
    context->width = V2D_DIMENSIONS_NOT_SET;
    context->height = V2D_DIMENSIONS_NOT_SET;
    spin_unlock_bh(&context->lock);

    return ret;
}

static int v2d_mmap(struct file *file, struct vm_area_struct *vma) {
    struct v2d_context *context = file->private_data;
    unsigned long pgoff = vma->vm_pgoff;
    unsigned long devsize, mapsize = vma->vm_end - vma->vm_start;
    unsigned long page, numpages;
    int ret;

    smp_mb();

    if (!atomic_read(&context->ready)) {
        return -EINVAL;
    }

    if (!(vma->vm_flags & VM_SHARED)) {
        return -ENOTSUP; // allowed by mman(3p)
    }

    devsize = (context->width * context->height + V2D_PAGESIZE - 1) / V2D_PAGESIZE;
    if (mapsize > V2D_PAGESIZE * devsize) {
        return -ENXIO;
    }

    numpages = (mapsize + V2D_PAGESIZE - 1) / V2D_PAGESIZE;
    if (pgoff > devsize / V2D_PAGESIZE || pgoff + numpages > devsize) {
        return -ENXIO;
    }

    vma->vm_flags |= VM_DONTEXPAND;
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    for (page = 0; page < numpages; ++page) {
        ret = vm_insert_page(vma, vma->vm_start + V2D_PAGESIZE * page, context->pages.pages[pgoff + page].page);
        if (IS_ERR_VALUE(ret)) {
            return ret;
        }
    }

    return 0;
}

/* context->write_lock must be held */
static struct v2d_command *v2d_get_next_command(struct v2d_context *context) {
    struct v2d_command *command = kzalloc(sizeof(*command), GFP_KERNEL);
    if (unlikely(!command)) {
        return ERR_PTR(-ENOMEM);
    }

    atomic_set(&command->refcount, 1);
    init_waitqueue_head(&command->queue);
    atomic_set(&command->ready, 0);
    command->context = context;

    return command;
}

// context->device->write_lock must be held
static int
v2d_submit(struct v2d_state *state, struct v2d_context *context, const char __user *buf, size_t count, size_t bufpos) {
    struct v2d_device *device = context->device;
    size_t i;

    if (unlikely(count == 0)) {
        return 0;
    }

    if (unlikely(copy_from_user(device->cmd_buf + bufpos, buf, count * sizeof(uint32_t)))) {
        return -EFAULT;
    }

    for (i = bufpos; i < bufpos + count; ++i) {
        int ret = v2d_validate(state, context, device->cmd_buf[i]);
        if (IS_ERR_VALUE(ret)) {
            return ret;
        }
    }

    return 0;
}

static inline size_t v2d_wrap(size_t pos) {
    if (unlikely(pos >= V2D_CMDBUF_SIZE - 1)) {
        pos -= V2D_CMDBUF_SIZE - 1;
    }
    return pos;
}

// device->write_lock must be (initally) held - and will be held again when
// v2d_wait_for_space exits with a zero return value
// mutex will not be held if an error occurs
static int v2d_wait_for_space(struct v2d_device *device) {
    size_t space = v2d_space(device);
    int ret;

    while (space < V2D_MIN_SPACE_FOR_COMMAND) {
        mutex_unlock(&device->write_lock);
        ret = wait_event_interruptible(device->write_queue, v2d_space(device) >= V2D_MIN_SPACE_FOR_COMMAND);
        if (IS_ERR_VALUE(ret)) {
            return ret;
        }
        mutex_lock(&device->write_lock);
        space = v2d_space(device);
    }

    return 0;
}

static ssize_t v2d_write(struct file *file, const char __user *buf, size_t size, loff_t *off) {
    struct v2d_context *context = file->private_data;
    struct v2d_device *device = context->device;
    struct v2d_state state;
    struct v2d_command *command;
    int ret;
    unsigned i;
    size_t count = size / sizeof(uint32_t);
    ssize_t space, start, to_write;

    if (unlikely(size == 0)) {
        return 0;
    }

    smp_mb();

    if (unlikely(!atomic_read(&context->ready) || size % sizeof(uint32_t) != 0)) {
        return -EINVAL;
    }

    mutex_lock(&device->write_lock);
    ret = v2d_wait_for_space(device);
    if (unlikely(IS_ERR_VALUE(ret))) {
        return ret;
    }

    command = v2d_get_next_command(context);
    if (unlikely(IS_ERR(command))) {
        ret = PTR_ERR(command);
        goto err_v2d_get_next_command;
    }

    space = v2d_space(device);
    BUG_ON(space < V2D_MIN_SPACE_FOR_COMMAND);

    state = context->state;
    start = device->write_index;

    if (device->last_context != context) {
#define v2d_write_push(cmd)                                                                                            \
    do {                                                                                                               \
        device->cmd_buf[start] = cmd;                                                                                  \
        start = v2d_wrap(start + 1);                                                                                   \
        space--;                                                                                                       \
    } while (0)

        v2d_write_push(VINTAGE2D_CMD_CANVAS_PT(context->pages.dma_addr, 0));
        v2d_write_push(VINTAGE2D_CMD_CANVAS_DIMS(context->width, context->height, 0));
        if (context->state.src_pos_cmd != V2D_INVALID_COMMAND)
            v2d_write_push(context->state.src_pos_cmd);
        if (context->state.dst_pos_cmd != V2D_INVALID_COMMAND)
            v2d_write_push(context->state.dst_pos_cmd);
        if (context->state.fill_color_cmd != V2D_INVALID_COMMAND)
            v2d_write_push(context->state.fill_color_cmd);
    }

    if (space < count + 4) {
        count = space - 4; // 1 accounts for COUNTER, + some safety buffer
    }
    to_write = count;

    for (i = 0; i < 2; ++i) {
        size_t left = v2d_space_own_end(device, start);
        if (count < left) {
            left = count;
        }

        ret = v2d_submit(&state, context, buf, left, start);
        if (IS_ERR_VALUE(ret)) {
            goto err_with_write_lock;
        }

        start = v2d_wrap(start + left);
        count -= left;
        buf += sizeof(uint32_t) * left;
        space -= left;
    }

    BUG_ON(space <= 0);
    BUG_ON(count > 0);

    device->cmd_buf[start] = VINTAGE2D_CMD_COUNTER(start, 1);
    command->counter = start;
    start = v2d_wrap(start + 1);

    device->write_index = start;
    device->last_context = context;

    spin_lock_bh(&device->lock);
    list_add_tail(&command->device_list, &device->current_commands);
    spin_unlock_bh(&device->lock);

    spin_lock_bh(&context->lock);
    context->state = state;
    list_add_tail(&command->context_list, &context->commands);
    spin_unlock_bh(&context->lock);

    mb();
    iowrite32(device->dma_cmd_buf + start * sizeof(uint32_t), device->bar0 + VINTAGE2D_CMD_WRITE_PTR);

    mutex_unlock(&device->write_lock);

    return to_write * sizeof(uint32_t);

err_v2d_get_next_command:
    kfree(command);
err_with_write_lock:
    mutex_unlock(&device->write_lock);
    return ret;
}

static int v2d_fsync(struct file *file, loff_t x, loff_t y, int dataflush) {
    struct v2d_context *context = file->private_data;
    int ret;

    smp_mb();
    if (unlikely(!atomic_read(&context->ready))) {
        return -EINVAL;
    }

    ret = v2d_wait_for_context_interruptible(context);
    mb();
    return ret;
}

struct file_operations v2d_fops = {
    .owner = THIS_MODULE,
    .open = v2d_open,
    .release = v2d_release,
    .mmap = v2d_mmap,
    .write = v2d_write,
    .fsync = v2d_fsync,
    .unlocked_ioctl = v2d_ioctl,
};
